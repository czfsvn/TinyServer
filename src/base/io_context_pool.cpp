#include "io_context_pool.h"
#include <stdexcept>
#include "Misc.h"
#include "TimeUtils.h"
#include "logger.h"
#include "timer_wheel.h"

namespace cncpp
{
    namespace
    {
        // Boost 1.70 前后 work guard 的构造方式不同，这里统一封装
        std::unique_ptr<IOContextWorkGuard> makeWorkGuard(boost::asio::io_context& ctx)
        {
#if BOOST_VERSION >= 107000
            return std::unique_ptr<IOContextWorkGuard>(new IOContextWorkGuard(boost::asio::make_work_guard(ctx)));
#else
            return std::unique_ptr<IOContextWorkGuard>(new IOContextWorkGuard(ctx));
#endif
        }
    }  // namespace

    uint32_t IOContextPool::tickIntervalMs() const
    {
        const uint32_t interval = interval_ms_.load(std::memory_order_relaxed);
        // tick 取 interval/5，最小 1ms：否则 expires_at 落在过去会变成无休眠的忙循环
        return interval > 0 ? std::max<uint32_t>(1, interval / 5) : 1;
    }

    bool IOContextPool::init()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (initialized_.load(std::memory_order_relaxed))
        {
            LOG_WARN("IOContextPool already initialized");
            return true;
        }

        uint16_t pool_size = sMainConfig.asio_pool_size();
        if (pool_size == 0)
        {
            pool_size = std::thread::hardware_concurrency();
            if (pool_size == 0)
            {
                pool_size = 4;  // 默认值
            }
        }

        try
        {
            io_contexts_.resize(pool_size);

            for (size_t i = 0; i < pool_size; ++i)
            {
                auto& wrapper   = io_contexts_[i];
                wrapper.context = std::make_shared<boost::asio::io_context>();
                wrapper.work    = makeWorkGuard(*wrapper.context);
            }

            // setTimerCallback() 显式指定的周期优先于配置；配置只作为默认值
            if (!has_explicit_interval_.load(std::memory_order_relaxed))
            {
                interval_ms_.store(sMainConfig.main_loop_interval_ms(), std::memory_order_relaxed);
                if (interval_ms_.load(std::memory_order_relaxed) == 0)
                {
                    interval_ms_.store(kDefaultIntervalMs, std::memory_order_relaxed);
                }
            }

            // 支持 cleanup() 之后重新 init()
            cleaned_.store(false, std::memory_order_relaxed);
            stop_state_.store(StopState::Running, std::memory_order_relaxed);
            running_.store(false, std::memory_order_relaxed);
            next_index_.store(0, std::memory_order_relaxed);
            last_call_back_.store(0, std::memory_order_relaxed);
            current_tick_ms_.store(0, std::memory_order_relaxed);
            // 必须在 runIOContextPools() 建线程之前清零，否则重 init 后计数对不上
            finished_threads_.store(0, std::memory_order_relaxed);
            next_expire_time_ = std::chrono::steady_clock::time_point{};

            if (USING_BOOST_ASIO_TIMER)
            {
                main_io_context_ = std::make_unique<boost::asio::io_context>();
                main_timer_      = std::make_unique<boost::asio::steady_timer>(*main_io_context_);
            }

            initialized_.store(true, std::memory_order_release);
            LOG_INFO("IOContextPool initialized with {} contexts, interval_ms={}", pool_size,
                     interval_ms_.load(std::memory_order_relaxed));

            runIOContextPools();
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to initialize IOContextPool: " + std::string(e.what()));
            io_contexts_.clear();
            return false;
        }
    }

    void IOContextPool::mainLoop()
    {
        const uint32_t tick_ms = tickIntervalMs();
        LOG_TRACE("[IOContextPool][startTimer] interval_ms_={}, tick_ms={}",
                  interval_ms_.load(std::memory_order_relaxed), tick_ms);

        if (USING_BOOST_ASIO_TIMER)
        {
            if (!main_timer_ || !main_io_context_)
            {
                throw std::runtime_error("Main timer or io_context not initialized");
            }

            // 使用绝对时间，避免累积误差
            auto now = std::chrono::steady_clock::now();

            // 第一次启动、或落后于当前时间（业务回调耗时过长/系统卡顿）时重置基准，
            // 否则会连续触发补偿导致 CPU 尖峰
            if (next_expire_time_.time_since_epoch().count() == 0 || next_expire_time_ <= now)
            {
                next_expire_time_ = now;
            }

            // 计算下次过期时间（绝对时间）
            next_expire_time_ += std::chrono::milliseconds(tick_ms);
            main_timer_->expires_at(next_expire_time_);
            main_timer_->async_wait([this](const boost::system::error_code& ec) { onMainTimer(ec); });
        }
        else
        {
            while (running_.load(std::memory_order_relaxed))
            {
                cncpp::sleepfor_milliseconds(tick_ms);
                updateTimer();
            }
        }
    }

    void IOContextPool::onMainTimer(const boost::system::error_code& ec)
    {
        // stop()/析构时会 cancel 定时器：不能续期，否则停止流程会被重新拉起
        if (ec)
        {
            if (ec != boost::asio::error::operation_aborted)
            {
                LOG_WARN("[IOContextPool][onMainTimer] timer error: {}", ec.message());
            }
            return;
        }

        updateTimer();
    }

    void IOContextPool::updateTimer()
    {
        const uint64_t now_ms = cncpp::getNowMilliSecond();
        current_tick_ms_.store(now_ms, std::memory_order_relaxed);

        // 拷贝出来再调用：避免持锁执行业务回调（回调里可能再次 setTimerCallback 造成死锁/阻塞）
        std::function<void()> callback;
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            callback = timer_callback_;
        }

        if (callback)
        {
            if (now_ms - last_call_back_.load(std::memory_order_relaxed)
                >= interval_ms_.load(std::memory_order_relaxed))
            {
                // 每拍一行 INFO 会把真正的业务日志淹掉；tick 打点只在排查抖动时用。
                LOG_TRACE("[IOContextPool][updateTimer] call_time={}", now_ms);
                try
                {
                    callback();
                }
                catch (const std::exception& e)
                {
                    // 业务异常必须拦住：否则异常穿出 io_context::run()，
                    // 定时器续期代码不会执行，主循环会静默停摆
                    LOG_ERROR("[IOContextPool][updateTimer] callback exception: {}", e.what());
                }
                catch (...)
                {
                    LOG_ERROR("[IOContextPool][updateTimer] callback unknown exception");
                }
                last_call_back_.store(now_ms, std::memory_order_relaxed);
            }
        }

        // 重新启动定时器，形成循环
        if (USING_BOOST_ASIO_TIMER && running_.load(std::memory_order_relaxed))
        {
            mainLoop();
        }
    }

    void IOContextPool::setTimerCallback(std::function<void()> callback, const uint32_t interval_ms /* = 1000*/)
    {
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            timer_callback_ = std::move(callback);
        }

        if (interval_ms > 0)
        {
            interval_ms_.store(interval_ms, std::memory_order_relaxed);
            has_explicit_interval_.store(true, std::memory_order_relaxed);
        }
    }

    boost::asio::io_context& IOContextPool::getMainIOContext()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_.load(std::memory_order_relaxed) || !main_io_context_)
        {
            throw std::runtime_error("IOContextPool not initialized");
        }

        return *main_io_context_;
    }

    boost::asio::io_context& IOContextPool::getIoContext()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_.load(std::memory_order_relaxed) || io_contexts_.empty())
        {
            throw std::runtime_error("IOContextPool not initialized");
        }

        // 使用简单的轮询策略
        size_t index = next_index_.fetch_add(1, std::memory_order_relaxed) % io_contexts_.size();
        return *io_contexts_[index].context;
    }

    boost::asio::io_context& IOContextPool::getIoContext(size_t index)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_.load(std::memory_order_relaxed))
        {
            throw std::runtime_error("IOContextPool not initialized");
        }

        if (index >= io_contexts_.size())
        {
            throw std::out_of_range("IOContextPool index out of range");
        }

        return *io_contexts_[index].context;
    }

    size_t IOContextPool::getPoolSize() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return io_contexts_.size();
    }

    void IOContextPool::runIOContextPools()
    {
        LOG_INFO("Starting IOContextPool with " + std::to_string(io_contexts_.size()) + " threads");

        for (size_t i = 0; i < io_contexts_.size(); ++i)
        {
            auto&       wrapper = io_contexts_[i];
            auto        ctx     = wrapper.context;  // shared_ptr 副本：detach 兜底时也不会悬空
            std::thread worker  = std::thread([i, ctx, this]() {
                try
                {
                    LOG_TRACE("IOContext thread {} started.", i);
                    LOG_DEBUG("IOContext thread {} entering run loop", i);
                    ctx->run();
                    LOG_TRACE("IOContext thread {} run loop exited", i);
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("IOContext thread {} error: {}", i, e.what());
                }

                // 正常或异常退出都要计数：joinAllThreads() 靠它精确等待线程真的跑完。
                // 不能用 joinable() 判断——线程跑完后它依然是 true。
                //
                // notify 必须放在锁内：主线程从 wait_for 返回前要重新拿到 mutex_，
                // 所以这里一定是先 notify 再被 detach。放在锁外的话，线程可能拖到
                // 对象已析构之后才访问 join_cv_，构成 use-after-free。
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    finished_threads_.fetch_add(1, std::memory_order_relaxed);
                    join_cv_.notify_all();
                }
            });
            wrapper.thread = std::move(worker);
        }
    }

    void IOContextPool::run()
    {
        LOG_INFO("[IOContextPool][run]");
        if (!initialized_.load(std::memory_order_acquire))
        {
            throw std::runtime_error("IOContextPool not initialized");
        }

        if (running_.exchange(true, std::memory_order_acq_rel))
        {
            LOG_WARN("IOContextPool already running");
            return;
        }

        if (USING_BOOST_ASIO_TIMER)
        {
            mainLoop();               // 挂上第一个定时器
            main_io_context_->run();  // 阻塞在此，直到 stop()
        }
        else
        {
            // 非 asio 分支的 mainLoop 自带阻塞循环，放到独立线程，
            // 保证 run() 的语义在所有分支下一致：只负责启动，不永久占用调用线程
            main_loop_thread_ = std::thread([this]() { mainLoop(); });
        }
    }

    void IOContextPool::stop()
    {
        if (!initialized_.load(std::memory_order_acquire))
        {
            return;
        }

        // 防止重复调用
        StopState expected = StopState::Running;
        if (!stop_state_.compare_exchange_strong(expected, StopState::Stopping))
        {
            LOG_DEBUG("IOContextPool already stopping or stopped");
            return;
        }

        LOG_INFO("Stopping IOContextPool");

        running_.store(false, std::memory_order_release);

        // 取一份 context 快照：io_contexts_ 可能正被 cleanup() 清空，
        // 不要在持锁状态下调用 stop()（handler 里可能反过来访问本池）
        std::vector<std::shared_ptr<boost::asio::io_context>> contexts;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            contexts.reserve(io_contexts_.size());
            for (auto& wrapper : io_contexts_)
            {
                if (wrapper.context)
                {
                    contexts.push_back(wrapper.context);
                }
                wrapper.work.reset();
            }
        }

        // 停止所有工作线程的 io_context（worker 线程随后退出 run()，由 cleanup() 等待收尾）
        for (auto& ctx : contexts)
        {
            ctx->stop();
        }

        // 停止主定时器和主 IO 上下文（mainLoop）
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (main_timer_)
            {
                boost::system::error_code ec;
                main_timer_->cancel(ec);
            }
            if (main_io_context_)
            {
                main_io_context_->stop();
            }
        }

        // stop_state_ 必须在 mutex_ 保护下修改，否则 cv_ 等待方会丢失唤醒
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_state_.store(StopState::Stopped, std::memory_order_release);
        }
        cv_.notify_all();

        LOG_INFO("IOContextPool stopped");
    }

    void IOContextPool::joinAllThreads()
    {
        // 先 join 非 asio 分支的 mainLoop 线程（它和 worker 一样受 running_ 控制）
        if (main_loop_thread_.joinable())
        {
            try
            {
                main_loop_thread_.join();
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Failed to join main loop thread: {}", e.what());
            }
        }

        const size_t total = io_contexts_.size();
        LOG_INFO("Waiting for {} worker threads to finish", total);

        // 等所有 worker 真正跑完，而不是轮询 joinable()（线程跑完后它依然是 true，
        // 只有 join/detach 才会变 false）。超时说明有 handler 卡死（死循环 / 阻塞 IO /
        // 等不到的锁）——此时绝不能 join，那会永久阻塞，整个关闭流程烂在半路。
        bool all_finished = true;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            all_finished = join_cv_.wait_for(lock, kJoinTimeout, [this, total]() {
                return finished_threads_.load(std::memory_order_relaxed) >= total;
            });
        }

        if (!all_finished)
        {
            LOG_ERROR("[IOContextPool] {} of {} worker threads still running after {}s timeout; "
                      "detaching them so shutdown can continue",
                      total - finished_threads_.load(std::memory_order_relaxed), total,
                      kJoinTimeout.count());
        }

        // 统一 detach：已结束的线程 detach 只是回收资源；没结束的也不会 UAF，
        // 因为每个线程都持有 io_context 的 shared_ptr 副本
        for (size_t i = 0; i < total; ++i)
        {
            auto& wrapper = io_contexts_[i];
            if (!wrapper.thread.joinable())
            {
                continue;
            }

            try
            {
                wrapper.thread.detach();
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Failed to detach worker thread {}: {}", i, e.what());
            }
        }

        LOG_INFO("All worker threads processed");
    }

    void IOContextPool::waitForStop()
    {
        if (!initialized_.load(std::memory_order_acquire))
        {
            return;
        }

        {
            std::unique_lock<std::mutex> lock(mutex_);
            // stop_state_ 的写入与这里共用 mutex_，因此不会丢失唤醒
            cv_.wait(lock, [this]() {
                return stop_state_.load(std::memory_order_acquire) >= StopState::Stopped;
            });
        }

        running_.store(false, std::memory_order_release);
        LOG_INFO("IOContextPool stopped");
    }

    bool IOContextPool::waitForStopWithTimeout(std::chrono::seconds timeout)
    {
        if (!initialized_.load(std::memory_order_acquire))
        {
            return true;
        }

        bool stopped;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stopped = cv_.wait_for(lock, timeout, [this]() {
                return stop_state_.load(std::memory_order_acquire) >= StopState::Stopped;
            });
        }

        if (!stopped)
        {
            LOG_WARN("IOContextPool wait for stop timeout");
            return false;
        }

        running_.store(false, std::memory_order_release);
        LOG_INFO("IOContextPool stopped");
        return true;
    }

    void IOContextPool::cleanup()
    {
        // 幂等保护
        bool expected = false;
        if (!cleaned_.compare_exchange_strong(expected, true))
        {
            return;
        }

        LOG_INFO("Cleaning up IOContextPool");

        // stop() 内部幂等：Running 才真正停止；Stopping 状态由对端完成，这里只需等待
        stop();

        // join 所有工作线程（join 过程中 worker 仍可写日志）
        joinAllThreads();

        // 所有工作线程已退出，标记 Logger 关闭
        cncpp::Logger::getMe().requestShutdown();

        // 清理其他资源
        {
            std::lock_guard<std::mutex> lock(mutex_);
            io_contexts_.clear();
            main_io_context_.reset();
            main_timer_.reset();

            initialized_.store(false, std::memory_order_release);
            next_index_.store(0, std::memory_order_relaxed);
            running_.store(false, std::memory_order_release);
            stop_state_.store(StopState::Cleaned, std::memory_order_release);
        }
        cv_.notify_all();

        std::cout << "IOContextPool cleaned up" << std::endl;
    }

    IOContextPool::~IOContextPool()
    {
        cleanup();
    }
}  // namespace cncpp

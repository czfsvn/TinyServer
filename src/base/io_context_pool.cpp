#include "io_context_pool.h"
#include <stdexcept>
#include "Misc.h"
#include "TimeUtils.h"
#include "logger.h"
#include "signal_handler.h"
#include "timer_wheel.h"

namespace cncpp
{
    bool IOContextPool::init()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (initialized_)
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
                wrapper.context = std::make_unique<boost::asio::io_context>();
                wrapper.work    = std::make_unique<boost::asio::io_context::work>(*wrapper.context);
            }

            interval_ms_ = sMainConfig.main_loop_interval_ms();
            if (interval_ms_ == 0)
            {
                interval_ms_ = 10;
            }

            if (USING_BOOST_ASIO_TIMER)
            {
                main_io_context_ = std::make_unique<boost::asio::io_context>();
                main_timer_      = std::make_unique<boost::asio::steady_timer>(*main_io_context_);
            }

            initialized_ = true;
            LOG_INFO("IOContextPool initialized with " + std::to_string(pool_size) + " contexts");

            // 自动初始化信号处理（使用独立线程，不依赖任何 io_context）
            // 这样可以避免信号处理与 io_context 停止之间的死锁
            if (!signal_handler_.init())
            {
                LOG_WARN("Failed to initialize signal handler (this may cause issues with graceful shutdown)");
            }
            else
            {
                // 设置默认的优雅关闭回调
                signal_handler_.setGracefulShutdownCallback([this]() {
                    LOG_INFO("Graceful shutdown requested via signal");
                    this->stop();
                });
                LOG_INFO("Signal handler initialized with default callback");
            }

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

    bool IOContextPool::initSignalHandler()
    {
        LOG_INFO("Initializing signal handler");

        // 使用成员变量 signal_handler_，传入第一个 io_context
        if (!signal_handler_.init(*io_contexts_[0].context))
        {
            LOG_ERROR("Failed to initialize signal handler");
            return false;
        }

        // 设置默认的优雅关闭回调
        signal_handler_.setGracefulShutdownCallback([this]() {
            LOG_INFO("Graceful shutdown requested via signal");
            this->stop();
        });

        LOG_INFO("Signal handler initialized");
        return true;
    }

    void IOContextPool::setGracefulShutdownCallback(std::function<void()> callback)
    {
        signal_handler_.setGracefulShutdownCallback(callback);
    }

    void IOContextPool::enableCoreDump()
    {
        signal_handler_.enableCoreDump();
        LOG_INFO("Core dump enabled");
    }

    void IOContextPool::setCoreDumpPath(const std::string& path)
    {
        signal_handler_.setCoreDumpPath(path);
    }

    void IOContextPool::setCustomSignalHandler(int signal, std::function<void()> handler)
    {
        signal_handler_.setCustomSignalHandler(signal, handler);
    }

    bool IOContextPool::isShutdownRequested() const
    {
        return signal_handler_.isShutdownRequested();
    }

    void IOContextPool::mainLoop()
    {
        LOG_TRACE("[IOContextPool][startTimer] interval_ms_={}", interval_ms_);
        if (USING_BOOST_ASIO_TIMER)
        {
            if (!main_timer_ || !main_io_context_)
            {
                throw std::runtime_error("Main timer or io_context not initialized");
            }

            // 使用绝对时间，避免累积误差
            auto now = std::chrono::steady_clock::now();

            // 如果是第一次启动或过期时间已过，使用当前时间作为基准
            if (next_expire_time_.time_since_epoch().count() == 0 || next_expire_time_ <= now)
            {
                next_expire_time_ = now;
            }

            // 计算下次过期时间（绝对时间）
            next_expire_time_ += std::chrono::milliseconds(interval_ms_ / 5);
            main_timer_->expires_at(next_expire_time_);
            main_timer_->async_wait(std::bind(&IOContextPool::updateTimer, this));
        }
        else
        {
            while (running_.load())
            {
                /* code */
                cncpp::sleepfor_milliseconds(interval_ms_ / 5);
                updateTimer();
            }
        }
    }

    void IOContextPool::updateTimer()
    {
        current_tick_ms_ = cncpp::getNowMilliSecond();
        if (timer_callback_)
        {
            if (current_tick_ms_ - last_call_back_ >= interval_ms_)
            {
                LOG_TRACE("[IOContextPool][updateTimer] call_time={}", current_tick_ms_);
                timer_callback_();
                last_call_back_ = current_tick_ms_;
            }
        }

        // 重新启动定时器，形成循环
        if (USING_BOOST_ASIO_TIMER && running_.load())
        {
            mainLoop();
        }
    }

    void IOContextPool::setTimerCallback(std::function<void()> callback, const uint32_t interval_ms /* = 1000*/)
    {
        timer_callback_ = callback;
        if (interval_ms > 0)
            interval_ms_ = interval_ms;
    }

    boost::asio::io_context& IOContextPool::getIoContext()
    {
        if (!initialized_)
        {
            throw std::runtime_error("IOContextPool not initialized");
        }

        // 使用简单的轮询策略
        size_t index = next_index_.fetch_add(1, std::memory_order_relaxed) % io_contexts_.size();
        return *io_contexts_[index].context;
    }

    boost::asio::io_context& IOContextPool::getIoContext(size_t index)
    {
        if (!initialized_)
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
        return io_contexts_.size();
    }

    void IOContextPool::runIOContextPools()
    {
        LOG_INFO("Starting IOContextPool with " + std::to_string(io_contexts_.size()) + " threads");

        // 修复后的代码（第119行附近）：
        for (size_t i = 0; i < io_contexts_.size(); ++i)
        {
            auto& wrapper  = io_contexts_[i];
            wrapper.thread = std::thread([i, this]() {
                try
                {
                    LOG_TRACE("IOContext thread " + std::to_string(i) + " started.");

                    // 通过索引安全访问，避免悬空引用
                    LOG_DEBUG("IOContext thread " + std::to_string(i) + " entering run loop");
                    io_contexts_[i].context->run();
                    LOG_TRACE("IOContext thread " + std::to_string(i) + " run loop exited");
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("IOContext thread " + std::to_string(i) + " error: " + e.what());
                }
            });
        }
    }

    void IOContextPool::run()
    {
        LOG_INFO("[IOContextPool][run]");
        if (!initialized_)
        {
            throw std::runtime_error("IOContextPool not initialized");
        }

        if (running_.exchange(true))
        {
            LOG_WARN("IOContextPool already running");
            return;
        }

        mainLoop();

        if (USING_BOOST_ASIO_TIMER)
        {
            main_io_context_->run();
        }
    }

    void IOContextPool::stop()
    {
        if (!initialized_)
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

        // 停止所有工作线程的 io_context
        for (auto& wrapper : io_contexts_)
        {
            if (wrapper.context)
            {
                wrapper.context->stop();
            }
            wrapper.work.reset();
        }

        // 停止主定时器和主 IO 上下文（mainLoop）
        if (main_timer_)
        {
            boost::system::error_code ec;
            main_timer_->cancel(ec);
        }
        if (main_io_context_)
        {
            main_io_context_->stop();
        }

        // 设置状态
        running_.store(false);
        stop_state_.store(StopState::Stopped);
        cv_.notify_all();

        LOG_INFO("IOContextPool stopped");
    }

    void IOContextPool::joinAllThreads()
    {
        LOG_INFO("Joining {} worker threads", io_contexts_.size());

        for (size_t i = 0; i < io_contexts_.size(); ++i)
        {
            auto& wrapper = io_contexts_[i];

            // 检查线程是否可 join（避免对已 detach 的线程操作）
            if (!wrapper.thread.joinable())
            {
                LOG_DEBUG("Worker thread {} not joinable, skipping", i);
                continue;
            }

            LOG_DEBUG("Joining worker thread {}", i);

            try
            {
                // 使用带超时的轮询方式，避免无限等待
                auto start    = std::chrono::steady_clock::now();
                bool detached = false;

                while (wrapper.thread.joinable())
                {
                    auto elapsed = std::chrono::steady_clock::now() - start;
                    if (elapsed >= std::chrono::seconds(2))
                    {
                        LOG_WARN("Worker thread {} join timeout after {}ms, detaching", i,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
                        wrapper.thread.detach();
                        detached = true;
                        break;
                    }

                    // 回退到短时间 sleep 后重试
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                if (!detached && !wrapper.thread.joinable())
                {
                    LOG_TRACE("Worker thread {} joined successfully", i);
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Failed to join worker thread {}: {}", i, e.what());
                // 尝试 detach 避免资源泄漏
                if (wrapper.thread.joinable())
                {
                    try
                    {
                        wrapper.thread.detach();
                    }
                    catch (...)
                    {
                    }
                }
            }
        }

        LOG_INFO("All worker threads processed");
    }

    void IOContextPool::waitForStop()
    {
        if (!initialized_)
        {
            return;
        }

        // 等待状态变为 Stopped（在锁内等待）
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return stop_state_.load() >= StopState::Stopped;
            });
        }

        running_.store(false);
        LOG_INFO("IOContextPool stopped");
    }

    bool IOContextPool::waitForStopWithTimeout(std::chrono::seconds timeout)
    {
        if (!initialized_)
        {
            return true;
        }

        // 等待状态变为 Stopped（在锁内等待）
        bool stopped;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stopped = cv_.wait_for(lock, timeout, [this]() {
                return stop_state_.load() >= StopState::Stopped;
            });
        }

        if (!stopped)
        {
            LOG_WARN("IOContextPool wait for stop timeout");
            return false;
        }

        running_.store(false);
        LOG_INFO("IOContextPool stopped");
        return true;
    }

    void IOContextPool::cleanup()
    {
        LOG_INFO("Cleaning up IOContextPool");

        // 如果还在运行，先停止
        if (stop_state_.load() == StopState::Running)
        {
            stop();
        }

        // 无论什么状态，都等待线程退出（不持有锁）
        joinAllThreads();

        // 先清理 SignalHandler（不持有锁）
        signal_handler_.cleanup();

        // 再清理其他资源（持有锁）
        {
            std::lock_guard<std::mutex> lock(mutex_);
            io_contexts_.clear();
            main_io_context_.reset();
            main_timer_.reset();

            initialized_ = false;
            next_index_.store(0);
            running_.store(false);
        }

        LOG_INFO("IOContextPool cleaned up");
    }

    IOContextPool::~IOContextPool()
    {
        cleanup();
    }
}  // namespace cncpp

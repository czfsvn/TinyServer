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

            interval_ms_ = sMainConfig.asio_timer_interval_ms();
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

    void IOContextPool::startTimer()
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
            startTimer();
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

        startTimer();
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

        // 使用原子操作检查并更新状态，防止重复调用
        StopState expected = StopState::Running;
        if (!stop_state_.compare_exchange_strong(expected, StopState::Stopping))
        {
            // 已经在停止或已停止
            LOG_DEBUG("IOContextPool already stopping or stopped, current state: {}",
                      static_cast<int>(stop_state_.load()));
            return;
        }

        LOG_INFO("Stopping IOContextPool");

        // 设置 running_ 为 false，阻止定时器重新启动
        running_.store(false);

        // 停止所有工作线程的 io_context（先停止，让线程开始退出）
        for (size_t i = 0; i < io_contexts_.size(); ++i)
        {
            if (io_contexts_[i].context)
            {
                io_contexts_[i].context->stop();
                LOG_TRACE("Worker io_context " << i << " stopped");
            }
        }

        // 移除所有 work 对象，允许 io_context 自然停止
        for (size_t i = 0; i < io_contexts_.size(); ++i)
        {
            io_contexts_[i].work.reset();
            LOG_TRACE("Work object " << i << " reset");
        }

        // 取消主定时器
        if (main_timer_)
        {
            boost::system::error_code ec;
            main_timer_->cancel(ec);
            LOG_TRACE("Main timer canceled");
        }

        // 停止主 IO 上下文（这会让 Run() 中的 main_io_context_->run() 返回）
        if (main_io_context_)
        {
            main_io_context_->stop();
            LOG_TRACE("Main io_context stopped");
        }

        // 更新状态为已停止
        stop_state_.store(StopState::Stopped);
        cv_.notify_all();

        LOG_INFO("IOContextPool stop signals sent");
    }

    void IOContextPool::waitForStop()
    {
        // 无限等待直到所有线程停止
        waitForStopWithTimeout(std::chrono::seconds::max());
    }

    bool IOContextPool::waitForStopWithTimeout(std::chrono::seconds timeout)
    {
        if (!initialized_)
        {
            return true;
        }

        // 等待状态变为 Stopped
        std::unique_lock<std::mutex> lock(mutex_);
        bool                         stopped = cv_.wait_for(lock, timeout, [this]() {
            return stop_state_.load() >= StopState::Stopped;
        });

        if (!stopped)
        {
            LOG_WARN("IOContextPool wait for stop timeout");
            return false;
        }

        // 等待所有工作线程退出
        bool all_stopped = true;
        auto start       = std::chrono::steady_clock::now();

        for (size_t i = 0; i < io_contexts_.size(); ++i)
        {
            auto& wrapper = io_contexts_[i];
            if (wrapper.thread.joinable())
            {
                auto remaining_timeout = timeout - (std::chrono::steady_clock::now() - start);
                if (remaining_timeout <= std::chrono::seconds(0))
                {
                    LOG_WARN("Timeout before joining thread {}", i);
                    all_stopped = false;
                    continue;
                }

                // 使用 try_join_for（C++20）或轮询等待
                bool joined = false;

// 尝试使用 C++20 的 try_join_for
#if __cpp_lib_thread_try_join_for >= 201911L
                joined = wrapper.thread.try_join_for(remaining_timeout);
#else
                // 回退到轮询方式
                auto thread_start = std::chrono::steady_clock::now();
                while (wrapper.thread.joinable())
                {
                    auto elapsed = std::chrono::steady_clock::now() - thread_start;
                    if (elapsed >= remaining_timeout)
                    {
                        LOG_WARN("Worker thread {} exit timeout", i);
                        all_stopped = false;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                joined = !wrapper.thread.joinable();
#endif

                if (joined)
                {
                    LOG_TRACE("Worker thread {} exited successfully", i);
                }
                else
                {
                    LOG_ERROR("Failed to join worker thread {}, attempting force termination", i);
// 尝试强制终止线程（不推荐，但作为最后的手段）
#ifdef _WIN32
                    TerminateThread(wrapper.thread.native_handle(), 0);
#else
                    pthread_cancel(wrapper.thread.native_handle());
#endif
                    // 等待线程终止
                    wrapper.thread.join();
                    LOG_WARN("Worker thread {} force terminated", i);
                    all_stopped = false;
                }
            }
        }

        running_.store(false);
        LOG_INFO("WaitForStopWithTimeout completed, all_stopped: {}", all_stopped);
        return all_stopped;
    }

    void IOContextPool::cleanup()
    {
        // 使用原子操作检查并更新状态，防止重复调用
        StopState expected = StopState::Stopped;
        if (!stop_state_.compare_exchange_strong(expected, StopState::Cleaned))
        {
            // 如果还在运行中，先停止
            if (stop_state_.load() == StopState::Running)
            {
                stop();
                waitForStop();
                stop_state_.store(StopState::Cleaned);
            }
            else if (stop_state_.load() == StopState::Stopping)
            {
                waitForStop();
                stop_state_.store(StopState::Cleaned);
            }
            else
            {
                // 已经清理过了
                std::cout << "IOContextPool already cleaned up" << std::endl;
                return;
            }
        }

        std::cout << "Cleaning up IOContextPool" << std::endl;

        {
            std::lock_guard<std::mutex> lock(mutex_);
#if 1
            // 清理所有 io_context
            io_contexts_.clear();
            main_io_context_.reset();
            main_timer_.reset();

            // 重置状态
            initialized_ = false;
            next_index_.store(0);
            running_.store(false);
#endif
        }

        std::cout << "IOContextPool cleaned up" << std::endl;
    }

    IOContextPool::~IOContextPool()
    {
        cleanup();
    }
}  // namespace cncpp

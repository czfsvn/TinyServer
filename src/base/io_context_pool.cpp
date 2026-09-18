#include "io_context_pool.h"
#include <stdexcept>
#include "Misc.h"
#include "TimeUtils.h"
#include "logger.h"
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
                // 每拍一行 INFO 会把真正的业务日志淹掉；tick 打点只在排查抖动时用。
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

    boost::asio::io_context& IOContextPool::getMainIOContext()
    {
        return *main_io_context_;
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

        for (size_t i = 0; i < io_contexts_.size(); ++i)
        {
            auto& wrapper  = io_contexts_[i];
            wrapper.thread = std::thread([i, this]() {
                try
                {
                    LOG_TRACE("IOContext thread " + std::to_string(i) + " started.");
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

            if (!wrapper.thread.joinable())
            {
                LOG_DEBUG("Worker thread {} not joinable, skipping", i);
                continue;
            }

            LOG_DEBUG("Joining worker thread {}", i);

            try
            {
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
        // 幂等保护
        bool expected = false;
        if (!cleaned_.compare_exchange_strong(expected, true))
        {
            return;
        }

        LOG_INFO("Cleaning up IOContextPool");

        if (stop_state_.load() == StopState::Running)
        {
            stop();
        }

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

            initialized_ = false;
            next_index_.store(0);
            running_.store(false);
        }

        std::cout << "IOContextPool cleaned up" << std::endl;
    }

    IOContextPool::~IOContextPool()
    {
        cleanup();
    }
}  // namespace cncpp

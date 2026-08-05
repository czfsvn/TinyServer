#ifndef IO_CONTEXT_POOL_H
#define IO_CONTEXT_POOL_H

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "network.h"
#include "singleton.h"

#define USING_BOOST_ASIO_TIMER 1

namespace cncpp
{
    class IOContextPool : public Singleton<IOContextPool>
    {
        friend class Singleton<IOContextPool>;

    public:
        // 初始化池（必须在程序开始时调用）
        bool init();

        // 获取一个io_context（轮询方式）
        boost::asio::io_context& getIoContext();

        // 获取指定索引的io_context
        boost::asio::io_context& getIoContext(size_t index);

        // 获取主 IO 上下文（用于定时器等特殊任务）
        boost::asio::io_context& getMainIOContext();

        // 获取池大小
        size_t getPoolSize() const;

        // 运行所有io_context（阻塞调用）
        void run();

        // 停止所有io_context
        void stop();

        // 等待所有io_context停止
        void waitForStop();

        // 等待所有io_context停止（带超时）
        bool waitForStopWithTimeout(std::chrono::seconds timeout);

        // 清理资源
        void cleanup();

    public:
        uint64_t getCurrentTickMs() const
        {
            return current_tick_ms_;
        }

    public:
        std::shared_ptr<cncpp::Acceptor> createAcceptor(short port, Acceptor::ConnectionCallback callback)
        {
            return std::make_shared<cncpp::Acceptor>(getIoContext(), port, callback);
        }

        std::shared_ptr<cncpp::Connector> createConnector()
        {
            return std::make_shared<cncpp::Connector>(getIoContext());
        }

        void setTimerCallback(std::function<void()> callback, const uint32_t interval_ms = 1000);

    private:
        IOContextPool() = default;
        ~IOContextPool();

        // 禁止拷贝和移动
        IOContextPool(const IOContextPool&)            = delete;
        IOContextPool& operator=(const IOContextPool&) = delete;
        IOContextPool(IOContextPool&&)                 = delete;
        IOContextPool& operator=(IOContextPool&&)      = delete;

        struct IOContextWrapper
        {
            std::unique_ptr<boost::asio::io_context>       context;
            std::unique_ptr<boost::asio::io_context::work> work;
            std::thread                                    thread;
        };

        void updateTimer();
        void mainLoop();

        void runIOContextPools();

        // 线程等待辅助函数
        void joinAllThreads();

    private:
        // 停止状态枚举
        enum class StopState
        {
            Running,   // 正常运行中
            Stopping,  // 正在停止
            Stopped,   // 已停止
            Cleaned    // 已清理
        };

        std::vector<IOContextWrapper> io_contexts_;
        std::atomic<size_t>           next_index_{0};
        std::atomic<bool>             running_{false};
        std::atomic<StopState>        stop_state_{StopState::Running};
        std::atomic<bool>             cleaned_{false};  // 防止 cleanup() 重复执行
        std::mutex                    mutex_;
        std::condition_variable       cv_;
        bool                          initialized_{false};

        // 主 IO 上下文，用于定时器等特殊任务
        uint32_t                                   interval_ms_{10};
        std::function<void()>                      timer_callback_;
        std::unique_ptr<boost::asio::steady_timer> main_timer_;
        std::unique_ptr<boost::asio::io_context>   main_io_context_;

        uint64_t                              last_call_back_{0};
        uint64_t                              current_tick_ms_ = 0;
        std::chrono::steady_clock::time_point next_expire_time_;
    };

}  // namespace cncpp

#define sIOContextPool cncpp::IOContextPool::getMe()

#endif  // IO_CONTEXT_POOL_H
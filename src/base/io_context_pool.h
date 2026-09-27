#ifndef IO_CONTEXT_POOL_H
#define IO_CONTEXT_POOL_H

#include <algorithm>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/version.hpp>
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
    // io_context::work 在 Boost 1.70 起废弃，改用 executor_work_guard
#if BOOST_VERSION >= 107000
    using IOContextWorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
#else
    using IOContextWorkGuard = boost::asio::io_context::work;
#endif

    class IOContextPool : public Singleton<IOContextPool>
    {
        friend class Singleton<IOContextPool>;

    public:
        // 默认主循环间隔（毫秒）：配置没设或读出来是 0 时用这个值兜底。
        // 成员初值与 init() 的回退逻辑共用它，避免 10 散落两处、改一处漏一处。
        static constexpr uint32_t kDefaultIntervalMs = 10;

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
            return current_tick_ms_.load(std::memory_order_relaxed);
        }

        // 实际生效的主循环间隔（毫秒）。配置为 0 时 init() 已回退到默认值，
        // 所以这里返回的永远是真正的 tick 周期，而不是配置原值
        uint32_t getIntervalMs() const
        {
            return interval_ms_.load(std::memory_order_relaxed);
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

        // context 用 shared_ptr：worker 线程持有副本，
        // 这样即使 join 失败需要 detach，也不会出现 io_context 被提前销毁的 UAF
        struct IOContextWrapper
        {
            std::shared_ptr<boost::asio::io_context> context;
            std::unique_ptr<IOContextWorkGuard>      work;
            std::thread                              thread;
        };

        // 主定时器 handler：负责过滤 error_code（cancel/aborted）后再驱动 updateTimer
        void onMainTimer(const boost::system::error_code& ec);

        // 一次 tick：更新 tick 时间戳 + 按 interval_ms_ 触发业务回调 + 续期定时器
        void updateTimer();

        // 启动/续期主定时器（asio 分支），或跑 sleep 循环（非 asio 分支）
        void mainLoop();

        // 单次 tick 的间隔 = interval_ms_ / 5，最小 1ms，避免 0 导致忙循环
        uint32_t tickIntervalMs() const;

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

        // mutex_ 保护 io_contexts_ / main_io_context_ / initialized_ 以及 stop_state_ 的写入
        // （stop_state_ 必须在同一把锁下修改，否则 cv_ 会丢失唤醒）
        std::vector<IOContextWrapper> io_contexts_;
        std::atomic<size_t>           next_index_{0};
        std::atomic<bool>             running_{false};
        std::atomic<StopState>        stop_state_{StopState::Running};
        std::atomic<bool>             cleaned_{false};  // 防止 cleanup() 重复执行
        mutable std::mutex            mutex_;
        std::condition_variable       cv_;
        std::atomic<bool>             initialized_{false};

        // 主 IO 上下文，用于定时器等特殊任务
        std::atomic<uint32_t>                      interval_ms_{kDefaultIntervalMs};
        std::atomic<bool>                          has_explicit_interval_{false};
        // timer_callback_ 由 timer_mutex_ 保护：可能在任意线程被替换，在 main io_context 线程被调用
        std::mutex                                 timer_mutex_;
        std::function<void()>                      timer_callback_;
        std::unique_ptr<boost::asio::steady_timer> main_timer_;
        std::unique_ptr<boost::asio::io_context>   main_io_context_;
        std::thread                                main_loop_thread_;  // 仅非 asio 定时器分支使用

        // join 超时兜底：等待 worker 真正跑完的最长时间。超时后 detach 并继续退出流程，
        // 否则一个卡死的 handler（死循环 / 阻塞 IO / 等不到的锁）会让关闭永久卡住。
        static constexpr std::chrono::seconds kJoinTimeout{3};

        // worker 线程退出计数。不能用 joinable() 判断线程是否结束：
        // 线程跑完后 joinable() 依然是 true，只有 join/detach 才会变 false。
        std::atomic<size_t>     finished_threads_{0};
        std::condition_variable join_cv_;

        std::atomic<uint64_t>                 last_call_back_{0};
        std::atomic<uint64_t>                 current_tick_ms_{0};
        std::chrono::steady_clock::time_point next_expire_time_;
    };

}  // namespace cncpp

#define sIOContextPool cncpp::IOContextPool::getMe()

#endif  // IO_CONTEXT_POOL_H
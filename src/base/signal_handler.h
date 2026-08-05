#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <atomic>
#include <boost/asio/signal_set.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace cncpp
{
    /// 普通信号处理器（SIGINT/SIGTERM/SIGHUP/SIGUSR1）
    /// 经 boost::asio::signal_set 异步处理，handler 运行在 io_context 线程上。
    ///
    /// 崩溃信号（SIGSEGV/SIGABRT 等）已移至 CrashHandler，用平台原生同步机制处理，
    /// 绝不经过 io_context（崩溃时 io_context 可能已不可用）。
    class SignalHandler
    {
    public:
        SignalHandler();
        ~SignalHandler();

        // 初始化信号处理（使用传入的 io_context）
        // signal_set 注册在此 io_context 上，
        // 信号到达时 handleSignal 作为 asio handler 在该 io_context 的线程上执行
        bool init(boost::asio::io_context& io_context);

        // 检查是否收到关闭信号
        bool isShutdownRequested() const;

        // 请求关闭
        void requestShutdown();

        // 清理资源（cancel signal_set 并 reset）
        // 注意：必须在 io_context 销毁之前调用，因为 signal_set 持有 io_context 的引用
        void cleanup();

        // 设置自定义信号处理回调
        void setCustomSignalHandler(int signal, std::function<void()> handler);

        // 设置优雅关闭回调
        void setGracefulShutdownCallback(std::function<void()> callback);

    private:
        // 信号处理函数（作为 asio handler 运行）
        void handleSignal(const boost::system::error_code& error, int signal_number);

        std::atomic<bool>                              shutdown_requested_;
        std::unique_ptr<boost::asio::signal_set>       signal_set_;
        std::function<void()>                          graceful_shutdown_callback_;
        std::unordered_map<int, std::function<void()>> custom_handlers_;
        bool                                           initialized_;
    };

}  // namespace cncpp

#endif  // SIGNAL_HANDLER_H

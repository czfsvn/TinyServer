#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <atomic>
#include <boost/asio/signal_set.hpp>
#include <functional>
#include <memory>
#include <thread>

namespace cncpp
{
    class SignalHandler
    {
    public:
        SignalHandler();
        ~SignalHandler();

        // 初始化信号处理（使用独立线程）
        bool init();

        // 初始化信号处理（使用传入的 io_context）
        bool init(boost::asio::io_context& io_context);

        // 检查是否收到关闭信号
        bool isShutdownRequested() const;

        // 请求关闭
        void requestShutdown();

        // 清理资源
        void cleanup();

        // 启用 core dump 生成
        void enableCoreDump();

        // 设置 core dump 文件路径
        void setCoreDumpPath(const std::string& path);

        // 设置自定义信号处理回调
        void setCustomSignalHandler(int signal, std::function<void()> handler);

        // 设置优雅关闭回调
        void setGracefulShutdownCallback(std::function<void()> callback);

    private:
        // 信号处理函数
        void handleSignal(const boost::system::error_code& error, int signal_number);

        // 运行信号处理循环
        void runSignalLoop();

        // 处理崩溃信号（生成 core dump）
        void handleCrashSignal(int signal_number);

        // 设置崩溃信号处理程序
        void setupCrashHandlers(boost::asio::io_context& io_context);

        std::atomic<bool>                              shutdown_requested_;
        std::unique_ptr<boost::asio::io_context>       own_io_context_;
        std::unique_ptr<boost::asio::io_context::work> own_work_;
        std::thread                                    own_thread_;
        std::unique_ptr<boost::asio::signal_set>       signal_set_;
        std::unique_ptr<boost::asio::signal_set>       crash_signal_set_;
        std::function<void()>                          graceful_shutdown_callback_;
        std::unordered_map<int, std::function<void()>> custom_handlers_;
        std::string                                    core_dump_path_;
        bool                                           initialized_;
        bool                                           core_dump_enabled_;
        bool                                           use_own_thread_;
    };

}  // namespace cncpp

#endif  // SIGNAL_HANDLER_H
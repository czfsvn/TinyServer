#include "signal_handler.h"
#include <csignal>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "logger.h"

#ifndef _WIN32
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace cncpp
{
    SignalHandler::SignalHandler()
        : shutdown_requested_(false),
          signal_set_(nullptr),
          crash_signal_set_(nullptr),
          graceful_shutdown_callback_(nullptr),
          initialized_(false),
          core_dump_enabled_(false),
          use_own_thread_(false)
    {
    }

    SignalHandler::~SignalHandler()
    {
        // Cleanup();
    }

    bool SignalHandler::init()
    {
        if (initialized_)
        {
            return true;
        }

        try
        {
            // 创建独立的 io_context 和工作对象
            own_io_context_ = std::make_unique<boost::asio::io_context>();
            own_work_       = std::make_unique<boost::asio::io_context::work>(*own_io_context_);

            signal_set_ = std::make_unique<boost::asio::signal_set>(*own_io_context_);

            // 注册要处理的信号
            signal_set_->add(SIGINT);   // Ctrl+C
            signal_set_->add(SIGTERM);  // 终止信号
#ifndef _WIN32
            signal_set_->add(SIGHUP);   // 终端挂起（Windows不支持）
            signal_set_->add(SIGUSR1);  // 用户自定义信号1（Windows不支持）
#endif

            // 开始异步信号处理
            signal_set_->async_wait([this](const boost::system::error_code& error, int signal) {
                handleSignal(error, signal);
            });

            // 设置崩溃信号处理程序
            crash_signal_set_ = std::make_unique<boost::asio::signal_set>(*own_io_context_);
#ifndef _WIN32
            // 注册崩溃信号
            crash_signal_set_->add(SIGSEGV);  // 段错误
            crash_signal_set_->add(SIGABRT);  // 异常终止
            crash_signal_set_->add(SIGFPE);   // 浮点异常
            crash_signal_set_->add(SIGILL);   // 非法指令
            crash_signal_set_->add(SIGBUS);   // 总线错误

            crash_signal_set_->async_wait([this](const boost::system::error_code& error, int signal) {
                if (!error)
                {
                    handleCrashSignal(signal);
                }
            });

            LOG_INFO("Crash signal handlers registered");
#endif

            // 启动独立线程来运行 io_context
            use_own_thread_ = true;
            own_thread_     = std::thread([this]() {
                LOG_INFO("Signal handler thread started");
                own_io_context_->run();
                LOG_INFO("Signal handler thread stopped");
            });

            initialized_ = true;
            LOG_INFO("Signal handler initialized successfully (using own thread)");
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to initialize signal handler: {}", e.what());
            cleanup();
            return false;
        }
    }

    bool SignalHandler::init(boost::asio::io_context& io_context)
    {
        if (initialized_)
        {
            return true;
        }

        try
        {
            signal_set_ = std::make_unique<boost::asio::signal_set>(io_context);

            // 注册要处理的信号
            signal_set_->add(SIGINT);   // Ctrl+C
            signal_set_->add(SIGTERM);  // 终止信号
#ifndef _WIN32
            signal_set_->add(SIGHUP);   // 终端挂起（Windows不支持）
            signal_set_->add(SIGUSR1);  // 用户自定义信号1（Windows不支持）
#endif

            // 开始异步信号处理
            signal_set_->async_wait([this](const boost::system::error_code& error, int signal) {
                handleSignal(error, signal);
            });

            // 设置崩溃信号处理程序
            setupCrashHandlers(io_context);

            initialized_ = true;
            LOG_INFO("Signal handler initialized successfully");
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to initialize signal handler: {}", e.what());
            return false;
        }
    }

    bool SignalHandler::isShutdownRequested() const
    {
        return shutdown_requested_.load();
    }

    void SignalHandler::requestShutdown()
    {
        shutdown_requested_.store(true);
    }

    void SignalHandler::cleanup()
    {
        if (!initialized_)
        {
            return;
        }

        if (signal_set_)
        {
            signal_set_->cancel();
        }

        if (crash_signal_set_)
        {
            crash_signal_set_->cancel();
        }

        // 如果使用了独立线程，需要停止并等待线程结束
        if (use_own_thread_ && own_io_context_)
        {
            own_work_.reset();        // 移除工作对象，允许 io_context 停止
            own_io_context_->stop();  // 停止 io_context

            if (own_thread_.joinable())
            {
                own_thread_.join();
            }
        }

        signal_set_.reset();
        crash_signal_set_.reset();
        own_io_context_.reset();
        own_work_.reset();
        initialized_ = false;
        std::cout << "Signal handler cleaned up\n";
    }

    void SignalHandler::enableCoreDump()
    {
#ifndef _WIN32
        struct rlimit rlim;
        rlim.rlim_cur = RLIM_INFINITY;
        rlim.rlim_max = RLIM_INFINITY;

        if (setrlimit(RLIMIT_CORE, &rlim) == 0)
        {
            core_dump_enabled_ = true;
            LOG_INFO("Core dump enabled successfully");
        }
        else
        {
            LOG_ERROR("Failed to enable core dump");
        }
#else
        LOG_WARN("Core dump is not supported on Windows");
#endif
    }

    void SignalHandler::setCoreDumpPath(const std::string& path)
    {
        core_dump_path_ = path;
        LOG_INFO("Core dump path set to: " + path);
    }

    void SignalHandler::setupCrashHandlers(boost::asio::io_context& io_context)
    {
#ifndef _WIN32
        crash_signal_set_ = std::make_unique<boost::asio::signal_set>(io_context);

        // 注册崩溃信号
        crash_signal_set_->add(SIGSEGV);  // 段错误
        crash_signal_set_->add(SIGABRT);  // 异常终止
        crash_signal_set_->add(SIGFPE);   // 浮点异常
        crash_signal_set_->add(SIGILL);   // 非法指令
        crash_signal_set_->add(SIGBUS);   // 总线错误

        crash_signal_set_->async_wait([this](const boost::system::error_code& error, int signal) {
            if (!error)
            {
                handleCrashSignal(signal);
            }
        });

        LOG_INFO("Crash signal handlers registered");
#endif
    }

    void SignalHandler::handleCrashSignal(int signal_number)
    {
#ifndef _WIN32
        // 生成 core dump 文件名
        std::time_t now        = std::time(nullptr);
        std::tm*    local_time = std::localtime(&now);

        std::ostringstream oss;
        if (!core_dump_path_.empty())
        {
            oss << core_dump_path_ << "/";
        }
        oss << "core." << getpid() << ".";
        oss << std::put_time(local_time, "%Y%m%d_%H%M%S");
        oss << "." << signal_number;

        std::string core_file = oss.str();

        LOG_ERROR("Crash signal received: " + std::to_string(signal_number));
        LOG_ERROR("Attempting to generate core dump: " + core_file);

        // 如果启用了 core dump，系统会自动生成
        // 这里我们只是记录日志并执行清理操作

        // 调用自定义崩溃处理程序（如果有）
        auto it = custom_handlers_.find(signal_number);
        if (it != custom_handlers_.end() && it->second)
        {
            it->second();
        }

        // 执行优雅关闭
        if (graceful_shutdown_callback_)
        {
            LOG_INFO("Executing graceful shutdown callback...");
            graceful_shutdown_callback_();
        }

        // 重新触发信号以生成 core dump
        std::signal(signal_number, SIG_DFL);
        std::raise(signal_number);
#endif
    }

    void SignalHandler::setCustomSignalHandler(int signal, std::function<void()> handler)
    {
        custom_handlers_[signal] = handler;
    }

    void SignalHandler::setGracefulShutdownCallback(std::function<void()> callback)
    {
        graceful_shutdown_callback_ = callback;
    }

    void SignalHandler::handleSignal(const boost::system::error_code& error, int signal_number)
    {
        if (!error)
        {
            LOG_INFO("Received signal: {}", signal_number);

            // 检查是否有自定义处理程序
            auto it = custom_handlers_.find(signal_number);
            if (it != custom_handlers_.end() && it->second)
            {
                it->second();
            }
            else
            {
                // 默认信号处理
                switch (signal_number)
                {
                    case SIGINT:   // Ctrl+C
                    case SIGTERM:  // 终止信号
                    {
                        // 使用 compare_exchange_strong 确保只处理一次关闭信号
                        bool expected = false;
                        if (!shutdown_requested_.compare_exchange_strong(expected, true))
                        {
                            // exchange 返回 false 表示已经是 true，说明是重复信号
                            LOG_WARN("Shutdown already in progress, ignoring duplicate signal");
                            break;
                        }

                        // exchange 返回 true 表示成功设置为 true，这是第一次收到信号
                        LOG_INFO("Shutdown signal received, initiating graceful shutdown...");

                        // 调用优雅关闭回调
                        if (graceful_shutdown_callback_)
                        {
                            graceful_shutdown_callback_();
                        }
                        break;
                    }
                    case SIGHUP:  // 终端挂起
                        LOG_INFO("SIGHUP received, reloading configuration...");
                        // 这里可以添加配置重载逻辑
                        break;
                    case SIGUSR1:  // 用户自定义信号1
                        LOG_INFO("SIGUSR1 received, performing maintenance...");
                        // 这里可以添加维护操作
                        break;
                    default:
                        LOG_WARN("Unhandled signal: {}", signal_number);
                        break;
                }
            }

            // 重新设置信号处理（除非是关闭信号）
            if (signal_set_ && !shutdown_requested_.load())
            {
                signal_set_->async_wait([this](const boost::system::error_code& error, int signal) {
                    handleSignal(error, signal);
                });
            }
        }
        else
        {
            LOG_ERROR("Signal handling error: " + error.message());
        }
    }

}  // namespace cncpp
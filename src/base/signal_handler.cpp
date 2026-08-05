#include "signal_handler.h"
#include <csignal>
#include <iostream>
#include "logger.h"

namespace cncpp
{
    SignalHandler::SignalHandler()
        : shutdown_requested_(false), signal_set_(nullptr), graceful_shutdown_callback_(nullptr), initialized_(false)
    {
    }

    SignalHandler::~SignalHandler()
    {
        // cleanup();
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

        initialized_ = false;

        // cancel 并销毁 signal_set
        // 注意：必须在 io_context 销毁之前调用，因为 signal_set 持有 io_context 的引用
        if (signal_set_)
        {
            signal_set_->cancel();
        }

        signal_set_.reset();

        std::cout << "Signal handler cleaned up\n";
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

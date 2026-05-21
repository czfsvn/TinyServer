#include <atomic>
#include <boost/asio/signal_set.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include "config.h"
#include "io_context_pool.h"
#include "logger.h"
#include "tinyclient.h"

// 全局客户端实例
std::unique_ptr<cncpp::TinyClient> g_client;

// 优雅关闭标志
std::atomic<bool> g_shutdown_completed{false};

// 优雅关闭函数
void GracefulShutdown()
{
    LOG_INFO("Starting graceful shutdown...");

    // 停止客户端
    if (g_client)
    {
        g_client->stop();
    }

    // 停止 IO 上下文池
    sIOContextPool.stop();

    // 等待 IO 上下文池停止
    sIOContextPool.waitForStop();

    // 关闭日志系统
    sLogger.shutdown();

    LOG_INFO("Graceful shutdown completed");

    // 设置优雅关闭完成标志
    g_shutdown_completed.store(true);
}

// 命令行输入处理线程
void commandInputThread()
{
    std::string input;
    std::cout << "\nTinyClient 命令行模式\n";
    std::cout << "输入 'help' 查看可用命令\n";
    std::cout << "------------------------\n";

    while (!g_shutdown_completed.load())
    {
        std::cout << "> ";
        std::getline(std::cin, input);

        if (input.empty())
            continue;

        // 处理命令
        if (g_client && g_client->getCommandHandler())
        {
            g_client->getCommandHandler()->executeCommand(input);
        }

        // 检查是否需要退出
        std::string lower_input = input;
        std::transform(lower_input.begin(), lower_input.end(), lower_input.begin(), ::tolower);
        if (lower_input == "quit" || lower_input == "exit")
        {
            break;
        }
    }
}

int main(int argc, char* argv[])
{
    // 读取配置
    if (!sConfig.ReadFromCommandLine(argc, argv))
    {
        std::cerr << "Failed to read config file\n";
        return 1;
    }

    // 初始化日志
    if (!sLogger.init())
    {
        std::cerr << "Failed to initialize logger\n";
        return 1;
    }

    // 初始化 IO 上下文池（会自动初始化信号处理）
    if (!sIOContextPool.init())
    {
        LOG_ERROR("Failed to initialize IOContext pool");
        return 1;
    }

    // 设置自定义优雅关闭回调（覆盖默认回调）
    sIOContextPool.setGracefulShutdownCallback(GracefulShutdown);

    // 启用 core dump 生成
    sIOContextPool.enableCoreDump();

    // 创建客户端
    g_client = std::make_unique<cncpp::TinyClient>();

    // 设置定时器回调来处理消息（类似 tinyserver 的方式）
    sIOContextPool.setTimerCallback(std::bind(&cncpp::TinyClient::processMessages, g_client.get()),
                                    g_client->getUpdateIntervalMs());

    // 启动 IO 上下文池
    std::thread pool_thread([]() {
        sIOContextPool.run();
    });

    // 启动命令行输入线程
    std::thread cmd_thread(commandInputThread);

    // 等待命令行线程结束
    cmd_thread.join();

    // 等待优雅关闭完成
    while (!g_shutdown_completed.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 等待 IO 线程结束
    pool_thread.join();

    return 0;
}
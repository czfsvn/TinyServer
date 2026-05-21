#include <boost/asio/signal_set.hpp>
#include <chrono>
#include <iostream>
#include "config.h"
#include "gateway_server.h"
#include "io_context_pool.h"
#include "logger.h"

// 全局网关实例
std::unique_ptr<GatewayServer> g_gateway;

// 优雅关闭标志
std::atomic<bool> g_shutdown_completed{false};

// 优雅关闭函数
void GracefulShutdown()
{
    LOG_INFO("Starting graceful shutdown...");

    // 停止网关服务器
    if (g_gateway)
    {
        g_gateway->Stop();
    }

    // 停止 IO 上下文池
    sIOContextPool.Stop();

    // 等待 IO 上下文池停止
    sIOContextPool.WaitForStop();

    // 关闭日志系统
    sLogger.shutdown();

    LOG_INFO("Graceful shutdown completed");

    // 设置优雅关闭完成标志
    g_shutdown_completed.store(true);
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
    if (!sIOContextPool.Initialize())
    {
        LOG_ERROR("Failed to initialize IOContext pool");
        return 1;
    }

    // 设置自定义优雅关闭回调（覆盖默认回调）
    sIOContextPool.SetGracefulShutdownCallback(GracefulShutdown);

    // 启用 core dump 生成
    sIOContextPool.EnableCoreDump();

    // 创建并启动网关服务器
    g_gateway = std::make_unique<GatewayServer>();
    if (!g_gateway->Start())
    {
        LOG_ERROR("Failed to start GatewayServer");
        return 1;
    }

    // 设置定时器回调来处理消息
    sIOContextPool.setTimerCallback(std::bind(&GatewayServer::processMessages, g_gateway.get()),
                                    g_gateway->getUpdateIntervalMs());

    // 运行 IO 上下文池
    sIOContextPool.Run();

    // 检查是否已经完成优雅关闭
    while (!g_shutdown_completed.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
#include <iostream>
#include "Misc.h"
#include "config.h"
#include "gateway_server.h"
#include "io_context_pool.h"
#include "logger.h"

int main(int argc, char* argv[])
{
    // 读取配置并启动服务
    if (!sGatewayServer.run(argc, argv))
    {
        std::cerr << "Failed to start GatewayServer\n";

        // 启动失败：Service::stop() 未被调用（is_running_ 为 false），
        // 需要手动清理 IO 线程和信号处理线程，再彻底关闭 logger。
        // 必须在 main() 返回前（全局析构开始前）完成，否则 spdlog 自身
        // 的全局静态析构顺序未定义，会导致段错误。
        sIOContextPool.cleanup();
        sLogger.shutdown();
        return 1;
    }

    // 阻塞等待信号回调设置 shutdown_requested_ 并唤醒 CV
    // 替代之前的 while(isRunning()) sleep(1s) 轮询，消除最多 1 秒的退出延迟
    sGatewayServer.wait();

    // 在主线程上执行全部清理（onStop → cleanup → signal_handler_.cleanup）
    // 此时 signal_handler_.cleanup() 从主线程调用，可安全 join 信号线程
    sGatewayServer.stop();

    // 在 main() 返回前（全局静态析构开始前）彻底关闭 logger。
    // sLogger.shutdown() 内部会调用 spdlog::shutdown()，必须在全局析构前完成。
    sLogger.shutdown();

    std::cout << "GatewayServer main() exit\n";
    return 0;
}

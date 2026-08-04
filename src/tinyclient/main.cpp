#include <iostream>
#include "client_service.h"
#include "config.h"
#include "io_context_pool.h"
#include "logger.h"

int main(int argc, char* argv[])
{
    // 启动服务（会阻塞直到服务停止）
    if (!sTinyClientService.run(argc, argv))
    {
        LOG_ERROR("Failed to start ClientService");
        sIOContextPool.cleanup();
        sLogger.shutdown();
        return 1;
    }

    // 阻塞等待信号回调唤醒，替代 while(isRunning()) sleep(1s) 轮询
    sTinyClientService.wait();

    // 在主线程上执行全部清理
    sTinyClientService.stop();

    // 在 main() 返回前彻底关闭 logger，避免全局析构时 spdlog 线程池
    // 析构顺序未定义导致 "async log/flush: thread pool doesn't exist anymore"
    sLogger.shutdown();

    std::cout << "TinyClient main() exit\n";
    return 0;
}

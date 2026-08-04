#include <iostream>
#include "Misc.h"
#include "config.h"
#include "io_context_pool.h"
#include "logger.h"
#include "tinyserver.h"

int main(int argc, char* argv[])
{
    // 读取配置
    if (!sTinyServer.run(argc, argv))
    {
        std::cerr << "Failed to read config file\n";
        sIOContextPool.cleanup();
        sLogger.shutdown();
        return 1;
    }

    // 阻塞等待信号回调唤醒，替代 while(isRunning()) sleep(1s) 轮询
    sTinyServer.wait();

    // 在主线程上执行全部清理
    sTinyServer.stop();

    // 在 main() 返回前彻底关闭 logger，避免全局析构时 spdlog 线程池
    // 析构顺序未定义导致 "async log/flush: thread pool doesn't exist anymore"
    sLogger.shutdown();
    std::cout << "tinyserver main() exit\n";
    return 0;
}

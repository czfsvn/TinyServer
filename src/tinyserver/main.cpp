#include <iostream>
#include "Misc.h"
#include "config.h"
#include "tinyserver.h"

int main(int argc, char* argv[])
{
    // 读取配置
    if (!sTinyServer.run(argc, argv))
    {
        std::cerr << "Failed to read config file\n";
        return 1;
    }

    while (sTinyServer.isRunning())
    {
        // 等待一小段时间，让正在进行的异步操作完成
        cncpp::sleepfor_seconds(1);
    }

    std::cout << "tinyserver main() exit\n";
    return 0;
}
#include <iostream>
#include "Misc.h"
#include "config.h"
#include "data_server.h"

int main(int argc, char* argv[])
{
    // 读取配置并启动服务
    if (!sDataServer.run(argc, argv))
    {
        std::cerr << "Failed to start DataServer\n";
        return 1;
    }

    // 主循环：等待服务停止
    while (sDataServer.isRunning())
    {
        // 等待一小段时间，让正在进行的异步操作完成
        cncpp::sleepfor_seconds(1);
    }

    std::cout << "DataServer main() exit\n";
    return 0;
}

#include <iostream>
#include "client_service.h"
#include "config.h"
#include "logger.h"

int main(int argc, char* argv[])
{
    // 启动服务（会阻塞直到服务停止）
    if (!sTinyClientService.run(argc, argv))
    {
        LOG_ERROR("Failed to start ClientService");
        return 1;
    }

    while (sTinyClientService.isRunning())
        std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "TinyClient main() exit\n";
    return 0;
}
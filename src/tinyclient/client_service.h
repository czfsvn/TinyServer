#pragma once

#include <atomic>
#include <thread>
#include "client_manager.h"
#include "service.h"
#include "singleton.h"


class ClientService : public cncpp::Service, public cncpp::Singleton<ClientService>
{
public:
    static constexpr size_t DEFAULT_CLIENT_COUNT = 4;

    ClientService();
    ~ClientService() override;

    ClientService(const ClientService&)            = delete;
    ClientService& operator=(const ClientService&) = delete;
    ClientService(ClientService&&)                 = delete;
    ClientService& operator=(ClientService&&)      = delete;

    bool init(size_t client_count = DEFAULT_CLIENT_COUNT);

    TinyClientPtr getClient(uint32_t index);
    TinyClientPtr getAvailableClient();

    /**
     * @brief 启动命令行输入线程
     */
    void startCommandThread();

    /**
     * @brief 停止命令行输入线程
     */
    void stopCommandThread();

protected:
    bool onInit() override;
    bool onStart() override;
    void onStop() override;
    bool onTick() override;

private:
    /**
     * @brief 命令行输入处理函数
     */
    void handleCommandInput();

    /**
     * @brief 打印帮助信息
     */
    void printHelp() const;

    size_t            client_count_{DEFAULT_CLIENT_COUNT};
    std::atomic<bool> cmd_thread_running_{false};
    std::thread       cmd_thread_;
};

#define sTinyClientService ClientService::getMe()

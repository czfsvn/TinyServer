#pragma once

#include <memory>
#include <string>
#include "command_handler.h"
#include "tcp_client.h"

class TinyClient : public cncpp::TcpClient
{
public:
    TinyClient();
    ~TinyClient() override;

    // 禁止拷贝和移动
    TinyClient(const TinyClient&)            = delete;
    TinyClient& operator=(const TinyClient&) = delete;
    TinyClient(TinyClient&&)                 = delete;
    TinyClient& operator=(TinyClient&&)      = delete;

    // 发送认证消息
    bool sendAuthMessage(uint32_t user_id);

    // 获取命令处理器
    CommandHandlerPtr getCommandHandler() const
    {
        return command_handler_;
    }

    // 获取更新间隔（毫秒）
    uint32_t getUpdateIntervalMs() const
    {
        return update_interval_ms_;
    }

    // 处理消息（定时器回调）
    void processMessages();

protected:
    void onConnectSuccess() override;
    void onConnectError(const std::string& error_msg) override;
    void onMessageReceived(const cncpp::NetworkMessage& message) override;
    void onDisconnected() override;

private:
    uint32_t         update_interval_ms_{10};
    CommandHandlerPtr command_handler_;
};

using TinyClientPtr = std::shared_ptr<TinyClient>;

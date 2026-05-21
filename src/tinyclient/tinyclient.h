#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include "command_handler.h"
#include "network.h"

class TinyClient
{
public:
    TinyClient();
    ~TinyClient();

    // 禁止拷贝和移动
    TinyClient(const TinyClient&)            = delete;
    TinyClient& operator=(const TinyClient&) = delete;
    TinyClient(TinyClient&&)                 = delete;
    TinyClient& operator=(TinyClient&&)      = delete;

    // 连接服务器
    bool connect();

    // 发送消息
    bool sendMessage(const std::string& message);

    // 发送认证消息
    bool sendAuthMessage(uint32_t user_id);

    // 停止客户端
    void stop();

    // 等待客户端停止
    void waitForStop();

    // 获取命令处理器
    CommandHandlerPtr getCommandHandler() const
    {
        return command_handler_;
    }

private:
    // 处理连接成功
    void onConnectSuccess(tcp::socket&& sock);

    // 处理连接错误
    void onConnectError(const std::string& error_msg);

    // 处理消息
    void onMessageReceived(const NetworkMessage& message);

public:
    // 获取更新间隔（毫秒）
    uint32_t getUpdateIntervalMs() const
    {
        return update_interval_ms_;
    }

private:
    // 处理消息（定时器回调）
    void processMessages();

private:
    std::shared_ptr<Connector> connector_;
    std::shared_ptr<Session>   session_;
    std::string                host_;
    short                      port_;
    uint32_t                   update_interval_ms_{10};  // 默认 10ms 间隔
    CommandHandlerPtr          command_handler_;         // 命令处理器
};
#include "tinyclient.h"
#include "io_context_pool.h"
#include "logger.h"

TinyClient::TinyClient() : host_(sNetworkConfig.host()), port_(sNetworkConfig.port())
{
    // 创建命令处理器
    command_handler_ = std::make_shared<CommandHandler>(*this);
}

TinyClient::~TinyClient()
{
    stop();
}

bool TinyClient::connect()
{
    try
    {
        LOG_INFO("Connecting to {}:{}", host_, port_);

        // 创建 connector
        connector_ = sIOContextPool.createConnector();
        if (!connector_)
        {
            LOG_ERROR("Failed to create connector");
            return false;
        }

        // 连接回调
        auto connect_cb = [this](tcp::socket&& sock) {
            onConnectSuccess(std::forward<tcp::socket>(sock));
        };

        // 错误回调
        auto error_cb = [this](const std::string& error_msg) {
            onConnectError(error_msg);
        };

        // 连接服务器
        connector_->connect(host_, port_, connect_cb, error_cb);

        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to connect: {}", e.what());
        return false;
    }
}

bool TinyClient::sendMessage(const std::string& message)
{
    if (!session_)
    {
        LOG_ERROR("Not connected to server");
        return false;
    }

    try
    {
        // 发送消息
        session_->send(message, 0);
        LOG_INFO("Sent message: {}", message);
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to send message: {}", e.what());
        return false;
    }
}

bool TinyClient::sendAuthMessage(uint32_t user_id)
{
    if (!session_)
    {
        LOG_ERROR("Not connected to server");
        return false;
    }

    try
    {
        // 创建认证消息
        NetworkMessage msg;
        msg.header_.msg_type_ = MessageType::AUTH;
        msg.header_.from_     = std::to_string(user_id);
        msg.body_             = std::to_string(user_id);

        session_->send(msg);
        LOG_INFO("Sent auth message for user: {}", user_id);
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to send auth message: {}", e.what());
        return false;
    }
}

void TinyClient::stop()
{
    if (session_)
    {
        session_->close();
        session_.reset();
    }

    if (connector_)
    {
        connector_.reset();
    }

    LOG_INFO("TinyClient stopped");
}

void TinyClient::waitForStop()
{
    // 等待 IO 上下文池停止
    sIOContextPool.waitForStop();
}

void TinyClient::onConnectSuccess(tcp::socket&& sock)
{
    // 创建会话
    session_ = std::make_shared<Session>(std::move(sock), nullptr);

    // 启动会话
    session_->start();

    LOG_INFO("Connected to server successfully");
}

void TinyClient::onConnectError(const std::string& error_msg)
{
    LOG_ERROR("Connection error: {}", error_msg);
}

void TinyClient::onMessageReceived(const NetworkMessage& message)
{
    // LOG_INFO("Received message from server:");
    // LOG_INFO("  Type: {}", static_cast<int>(message.header_.msg_type_));
    // LOG_INFO("  From: {}", message.header_.from_);
    // LOG_INFO("  To: {}", message.header_.to_);
    // LOG_INFO("  Body: {}", message.body_);
}

void TinyClient::processMessages()
{
    if (!session_)
    {
        return;
    }

    // 处理队列中的所有消息
    NetworkMessage message;
    while (session_->getReceiveQueue().tryPop(message))
    {
        onMessageReceived(message);
    }
}

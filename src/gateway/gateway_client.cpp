#include "gateway_client.h"
#include "logger.h"

GatewayClient::GatewayClient()
{
}

GatewayClient::~GatewayClient()
{
}

void GatewayClient::setGatewayID(uint32_t gateway_id)
{
    gateway_id_ = gateway_id;
}

void GatewayClient::onConnectSuccess()
{
    // 调用父类方法
    cncpp::TcpClient::onConnectSuccess();

    LOG_INFO("GatewayClient connected to tinyserver successfully");

    // 发送网关注册消息
    sendRegisterMessage();
}

void GatewayClient::onMessageReceived(const cncpp::NetworkMessage& message)
{
    // 调用父类方法（会触发消息回调）
    cncpp::TcpClient::onMessageReceived(message);

    // 网关特定的消息处理可以在这里添加
    // LOG_DEBUG("GatewayClient processed message from tinyserver, type: {}", static_cast<int>(message.header_.msg_type_));
}

void GatewayClient::onDisconnected()
{
    LOG_WARN("GatewayClient disconnected from tinyserver");

    // 调用父类方法（会触发断开回调）
    cncpp::TcpClient::onDisconnected();
}

void GatewayClient::sendRegisterMessage()
{
    if (!isConnected())
    {
        LOG_ERROR("Cannot send register message, not connected");
        return;
    }

    cncpp::NetworkMessage register_msg;
    register_msg.header_.msg_type_ = cncpp::MessageType::REGISTER;
    register_msg.header_.from_     = gateway_id_;
    register_msg.header_.to_       = "server";
    register_msg.body_             = "gateway_register";

    sendMessage(register_msg);
    // LOG_INFO("GatewayClient sent register message, gateway_id: {}", register_msg.header_.from_);
}
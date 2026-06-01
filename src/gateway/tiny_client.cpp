#include "tiny_client.h"
#include "logger.h"
#include "network.h"

TinyClient::TinyClient() : gateway_id_(0)
{
}

TinyClient::~TinyClient()
{
    disconnect();
}

void TinyClient::setGatewayID(uint32_t gateway_id)
{
    gateway_id_ = gateway_id;
}

bool TinyClient::sendLoginRequest(uint32_t user_id, const std::string& token)
{
    if (!isConnected())
    {
        LOG_ERROR("TinyClient not connected, cannot send login request");
        return false;
    }

    cncpp::NetworkMessage message;
    message.header_.message_id_ = 1001;  // LOGIN_REQUEST

    bool result = sendMessage(message);
    if (result)
    {
        LOG_DEBUG("Sent login request for user: {}", user_id);
    }
    else
    {
        LOG_ERROR("Failed to send login request for user: {}", user_id);
    }

    return result;
}

bool TinyClient::sendHeartbeat()
{
    if (!isConnected())
    {
        LOG_ERROR("TinyClient not connected, cannot send heartbeat");
        return false;
    }

    cncpp::NetworkMessage message;
    message.header_.message_id_ = 1002;  // HEARTBEAT
    //message.body_                = std::to_string(gateway_id_);

    bool result = sendMessage(message);
    if (result)
    {
        last_heartbeat_time_
            = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                  .count();
        LOG_DEBUG("Sent heartbeat from gateway: {}", gateway_id_);
    }

    return result;
}

bool TinyClient::sendUserMessage(const cncpp::NetworkMessage& message)
{
    if (!isConnected())
    {
        LOG_ERROR("TinyClient not connected, cannot send user message");
        return false;
    }

    bool result = sendMessage(message);
    if (result)
    {
        LOG_DEBUG("Sent user message, msg_id: {}", message.header_.message_id_);
    }
    else
    {
        LOG_ERROR("Failed to send user message, msg_id: {}", message.header_.message_id_);
    }

    return result;
}

void TinyClient::onConnectSuccess()
{
    LOG_INFO("TinyClient connected to TinyServer: {}:{}", getServerHost(), getServerPort());
    sendRegisterMessage();
}

void TinyClient::onConnectError(const std::string& error_msg)
{
    LOG_ERROR("TinyClient connect error: {}", error_msg);
}

void TinyClient::onMessageReceived(const cncpp::NetworkMessage& message)
{
    LOG_DEBUG("TinyClient received message, msg_id: {}", message.header_.message_id_);

    switch (message.header_.message_id_)
    {
        case 1001:  // LOGIN_RESPONSE
            handleLoginResponse(message);
            break;
        case 1002:  // HEARTBEAT_RESPONSE
            handleHeartbeatResponse(message);
            break;
        case 2000:  // USER_MESSAGE
            handleUserMessage(message);
            break;
        default:
            LOG_WARN("TinyClient received unknown message type: {}", message.header_.message_id_);
            break;
    }
}

void TinyClient::onDisconnected()
{
    LOG_INFO("TinyClient disconnected from TinyServer");
    registered_ = false;
}

void TinyClient::sendRegisterMessage()
{
    cncpp::NetworkMessage message;
    message.header_.message_id_ = 1000;  // REGISTER_REQUEST
    //message.body_                = std::to_string(gateway_id_);

    bool result = sendMessage(message);
    if (result)
    {
        LOG_DEBUG("Sent register request for gateway: {}", gateway_id_);
    }
    else
    {
        LOG_ERROR("Failed to send register request");
    }
}

void TinyClient::handleLoginResponse(const cncpp::NetworkMessage& message)
{
    LOG_DEBUG("Received login response: {}", message.header_.message_id_);
}

void TinyClient::handleHeartbeatResponse(const cncpp::NetworkMessage& message)
{
    LOG_DEBUG("Received heartbeat response");
}

void TinyClient::handleUserMessage(const cncpp::NetworkMessage& message)
{
    LOG_DEBUG("Received user message: {}", message.header_.message_id_);
}

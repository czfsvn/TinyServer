#include "tinyclient.h"
#include "config.h"
#include "logger.h"
#include "message_ids.h"

TinyClient::TinyClient() : cncpp::TcpClient()
{
    command_handler_ = std::make_shared<CommandHandler>(*this);
}

TinyClient::~TinyClient()
{
    disconnect();
}

bool TinyClient::sendAuthMessage(uint32_t user_id)
{
    if (!isConnected())
    {
        LOG_ERROR("TinyClient not connected, cannot send auth message");
        return false;
    }

    cncpp::NetworkMessage msg;
    msg.header_.message_id_  = toUint32(MsgID::AUTH);
    msg.header_.body_length_ = sizeof(user_id);
    msg.setBody(std::to_string(user_id));

    bool result = sendMessage(msg);
    if (result)
    {
        LOG_INFO("Sent auth message for user: {}", user_id);
    }
    else
    {
        LOG_ERROR("Failed to send auth message for user: {}", user_id);
    }

    return result;
}

void TinyClient::processMessages()
{
    if (!getSession())
    {
        return;
    }

    // 每拍每个连接的处理上限，0 = 不限（抽干）。配置说明见 config.h。
    const uint32_t budget = sMainConfig.max_messages_per_task_per_tick();

    // 已断开的连接必须抽干：留到下一拍就没人再取了（A6）。
    const bool drain_all = (budget == 0) || !isConnected();

    cncpp::NetworkMessage message;
    uint32_t              handled = 0;
    while (getSession()->getReceiveQueue().pop(message))
    {
        onMessageReceived(message);

        if (!drain_all && ++handled >= budget)
        {
            break;
        }
    }
}

void TinyClient::onConnectSuccess()
{
    LOG_INFO("TinyClient connected to {}:{}", getServerHost(), getServerPort());
}

void TinyClient::onConnectError(const std::string& error_msg)
{
    LOG_ERROR("TinyClient connection error: {}", error_msg);
}

void TinyClient::onMessageReceived(const cncpp::NetworkMessage& message)
{
    if (command_handler_)
    {
        //todo: finish me
        //command_handler_->executeCommand(message);
    }
}

void TinyClient::onDisconnected()
{
    LOG_INFO("TinyClient disconnected from {}:{}", getServerHost(), getServerPort());
}

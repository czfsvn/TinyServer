#include "data_client.h"
#include "logger.h"
#include "network.h"

DataClient::DataClient()
{
}

DataClient::~DataClient()
{
    disconnect();
}

bool DataClient::sendPing()
{
    if (!isConnected())
    {
        LOG_ERROR("DataClient not connected, cannot send ping");
        return false;
    }

    cncpp::NetworkMessage message;
    message.header_.message_id_ = 3000;  // PING_REQUEST

    bool result = sendMessage(message);
    if (result)
    {
        last_ping_time_
            = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                  .count();
        LOG_DEBUG("Sent ping to DataServer");
    }

    return result;
}

void DataClient::onConnectSuccess()
{
    LOG_INFO("DataClient connected to DataServer: {}:{}", getServerHost(), getServerPort());
}

void DataClient::onConnectError(const std::string& error_msg)
{
    LOG_ERROR("DataClient connect error: {}", error_msg);
}

void DataClient::onMessageReceived(const cncpp::NetworkMessage& message)
{
    LOG_DEBUG("DataClient received message, msg_id: {}", message.header_.message_id_);

    switch (message.header_.message_id_)
    {
        case 3000:  // PING_RESPONSE
            handlePingResponse(message);
            break;
        default:
            LOG_WARN("DataClient received unknown message type: {}", message.header_.message_id_);
            break;
    }
}

void DataClient::onDisconnected()
{
    LOG_INFO("DataClient disconnected from DataServer");
}

void DataClient::handlePingResponse(const cncpp::NetworkMessage& message)
{
    LOG_DEBUG("Received ping response");
}

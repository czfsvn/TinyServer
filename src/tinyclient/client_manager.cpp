#include "client_manager.h"
#include "logger.h"

ClientManager::ClientManager()
{
}

ClientManager::~ClientManager()
{
    disconnectAll();
}

bool ClientManager::init(size_t client_count)
{
    if (client_count == 0)
    {
        LOG_ERROR("ClientManager: client_count cannot be 0");
        return false;
    }

    clients_.clear();
    clients_.reserve(client_count);

    for (size_t i = 0; i < client_count; ++i)
    {
        auto client = std::make_shared<TinyClient>();
        clients_.push_back(client);
    }

    LOG_INFO("ClientManager initialized with {} clients", client_count);
    return true;
}

bool ClientManager::connectAll()
{
    size_t connected = 0;

    for (size_t i = 0; i < clients_.size(); ++i)
    {
        auto& client = clients_[i];
        if (client->connect(sGatewayConfig.listen_host(), sGatewayConfig.listen_port()))
        {
            LOG_INFO("TinyClient[{}] connecting...", i);
            connected++;
        }
        else
        {
            LOG_ERROR("TinyClient[{}] failed to connect", i);
        }
    }

    LOG_INFO("ClientManager: {}/{} clients connected", connected, clients_.size());
    return connected > 0;
}

void ClientManager::disconnectAll()
{
    for (auto& client : clients_)
    {
        if (client)
        {
            client->disconnect();
        }
    }
    clients_.clear();
    LOG_INFO("ClientManager: all clients disconnected");
}

TinyClientPtr ClientManager::getClient(uint32_t index)
{
    if (index >= clients_.size())
    {
        return nullptr;
    }
    return clients_[index];
}

TinyClientPtr ClientManager::getAvailableClient()
{
    if (clients_.empty())
    {
        return nullptr;
    }

    size_t start_index = next_client_index_.fetch_add(1) % clients_.size();

    for (size_t i = 0; i < clients_.size(); ++i)
    {
        size_t index = (start_index + i) % clients_.size();
        if (clients_[index]->isConnected())
        {
            return clients_[index];
        }
    }

    return nullptr;
}

size_t ClientManager::getClientCount() const
{
    return clients_.size();
}

size_t ClientManager::getConnectedCount() const
{
    size_t count = 0;
    for (const auto& client : clients_)
    {
        if (client->isConnected())
        {
            count++;
        }
    }
    return count;
}

void ClientManager::processAllMessages()
{
    for (auto& client : clients_)
    {
        if (client->isConnected())
        {
            client->processMessages();
        }
    }
}

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "singleton.h"
#include "tinyclient.h"

class ClientManager : public cncpp::Singleton<ClientManager>
{
public:
    ClientManager();
    ~ClientManager();

    ClientManager(const ClientManager&)            = delete;
    ClientManager& operator=(const ClientManager&) = delete;
    ClientManager(ClientManager&&)                 = delete;
    ClientManager& operator=(ClientManager&&)      = delete;

    bool init(size_t client_count);
    bool connectAll();
    void disconnectAll();

    TinyClientPtr getClient(uint32_t index);
    TinyClientPtr getAvailableClient();
    size_t        getClientCount() const;
    size_t        getConnectedCount() const;

    void processAllMessages();

private:
    std::vector<TinyClientPtr> clients_;
    std::atomic<size_t>        next_client_index_{0};
};

#define sTinyClientManager ClientManager::getMe()

#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <string>
#include <thread>
#include "data_client.h"
#include "network.h"
#include "service.h"
#include "singleton.h"
#include "tiny_client.h"

class GatewayServer : public cncpp::Service, public cncpp::Singleton<GatewayServer>
{
public:
    static constexpr uint32_t MAX_RETRY_ATTEMPTS = 5;
    static constexpr uint32_t RETRY_INTERVAL_MS  = 3000;

    GatewayServer();
    ~GatewayServer();

    GatewayServer(const GatewayServer&)            = delete;
    GatewayServer& operator=(const GatewayServer&) = delete;
    GatewayServer(GatewayServer&&)                 = delete;
    GatewayServer& operator=(GatewayServer&&)      = delete;

    bool onInit() override;
    bool onStart() override;
    void onStop() override;
    bool onTick() override;

    TinyClientPtr getTinyClient() const
    {
        return tiny_client_;
    }
    DataClientPtr getDataClient() const
    {
        return data_client_;
    }

private:
    void finalAll();

    bool initTinyClient();
    bool initDataClient();

    bool connectToBackendServers();
    bool checkBackendReady();

    void onTinyClientConnected(bool success, const std::string& error);
    void onTinyClientDisconnected();
    void onTinyClientMessage(const cncpp::NetworkMessage& message);

    void onDataClientConnected(bool success, const std::string& error);
    void onDataClientDisconnected();
    void onDataClientMessage(const cncpp::NetworkMessage& message);

    void onClientConnected(tcp::socket&& sock);

    void stopAcceptor();
    bool initAcceptor();
    bool startAcceptor();

    void closeAllSessions();

private:
    std::shared_ptr<cncpp::Acceptor> acceptor_;
    TinyClientPtr                    tiny_client_;
    DataClientPtr                    data_client_;

    std::atomic<bool> backend_ready_{false};

    uint32_t backend_retry_count_{0};
};

#define sGatewayServer GatewayServer::getMe()

#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include "network.h"
#include "service.h"
#include "singleton.h"

class DataServer : public cncpp::Service, public cncpp::Singleton<DataServer>
{
public:
    DataServer();
    ~DataServer();

    DataServer(const DataServer&)            = delete;
    DataServer& operator=(const DataServer&) = delete;
    DataServer(DataServer&&)                 = delete;
    DataServer& operator=(DataServer&&)      = delete;

    bool onInit() override;
    bool onStart() override;
    void onStop() override;
    void onTick() override;

private:
    void onConnectionCreated(tcp::socket&& sock);

    bool initAcceptor();
    bool startAcceptor();
    void stopAcceptor();
    void closeAllSessions();

    bool checkMysql();
    bool syncTableSchema();

private:
    std::shared_ptr<cncpp::Acceptor> acceptor_;
    uint32_t                         port_{3307};
};

#define sDataServer DataServer::getMe()

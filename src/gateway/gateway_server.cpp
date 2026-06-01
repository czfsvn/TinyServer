#include "gateway_server.h"
#include "Misc.h"
#include "gate_task_manager.h"
#include "gate_user_manager.h"
#include "io_context_pool.h"
#include "logger.h"

GatewayServer::GatewayServer()
{
}

GatewayServer::~GatewayServer()
{
}

bool GatewayServer::onInit()
{
    LOG_INFO("GatewayServer initializing...");

    if (!initAcceptor())
    {
        LOG_ERROR("Failed to init acceptor");
        return false;
    }

    if (!initTinyClient())
    {
        LOG_ERROR("Failed to init TinyClient");
        return false;
    }

    if (!initDataClient())
    {
        LOG_ERROR("Failed to init DataClient");
        return false;
    }

    sGateTaskManager.startTaskScheduler();

    LOG_INFO("GatewayServer initialized successfully");
    return true;
}

bool GatewayServer::onStart()
{
    LOG_INFO("GatewayServer starting...");

    if (!connectToBackendServers())
    {
        LOG_ERROR("Failed to connect to backend servers");
        return false;
    }

    const uint32_t max_retry_count = 10;
    uint32_t       retry_count     = 0;
    bool           is_ready        = false;
    while (retry_count < max_retry_count)
    {
        is_ready = checkBackendReady();
        if (checkBackendReady())
        {
            is_ready = true;
            break;
        }

        cncpp::sleepfor_seconds(3);
        retry_count++;
    }

    if (!is_ready)
    {
        LOG_ERROR("Failed to establish connections to backend servers after {} retries", max_retry_count);
        return false;
    }
    else
    {
        LOG_INFO("Backend connections established successfully");
    }

    if (!startAcceptor())
    {
        LOG_ERROR("Failed to start acceptor");
        return false;
    }

    LOG_INFO("GatewayServer started successfully");
    return true;
}

void GatewayServer::onStop()
{
    LOG_INFO("GatewayServer stopping...");

    sGateTaskManager.stopTaskScheduler();

    if (tiny_client_)
    {
        tiny_client_->disconnect();
        tiny_client_.reset();
    }

    if (data_client_)
    {
        data_client_->disconnect();
        data_client_.reset();
    }

    stopAcceptor();
    closeAllSessions();
    finalAll();

    LOG_INFO("GatewayServer stopped");
}

bool GatewayServer::initTinyClient()
{
    try
    {
        tiny_client_ = std::make_shared<TinyClient>();
        if (!tiny_client_)
        {
            LOG_ERROR("Failed to create TinyClient");
            return false;
        }

        tiny_client_->setGatewayID(1);

        tiny_client_->setConnectCallback(
            std::bind(&GatewayServer::onTinyClientConnected, this, std::placeholders::_1, std::placeholders::_2));
        tiny_client_->setMessageCallback(std::bind(&GatewayServer::onTinyClientMessage, this, std::placeholders::_1));
        tiny_client_->setDisconnectCallback(std::bind(&GatewayServer::onTinyClientDisconnected, this));

        LOG_INFO("TinyClient initialized");
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to initialize TinyClient: {}", e.what());
        return false;
    }
}

bool GatewayServer::initDataClient()
{
    try
    {
        data_client_ = std::make_shared<DataClient>();
        if (!data_client_)
        {
            LOG_ERROR("Failed to create DataClient");
            return false;
        }

        data_client_->setConnectCallback(
            std::bind(&GatewayServer::onDataClientConnected, this, std::placeholders::_1, std::placeholders::_2));
        data_client_->setMessageCallback(std::bind(&GatewayServer::onDataClientMessage, this, std::placeholders::_1));
        data_client_->setDisconnectCallback(std::bind(&GatewayServer::onDataClientDisconnected, this));

        LOG_INFO("DataClient initialized");
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to initialize DataClient: {}", e.what());
        return false;
    }
}

bool GatewayServer::connectToBackendServers()
{
    const std::string& tiny_host = sNetworkConfig.server_host();
    short              tiny_port = sNetworkConfig.server_port();

    LOG_INFO("Connecting to TinyServer: {}:{}...", tiny_host, tiny_port);
    bool tiny_result = tiny_client_->connect(tiny_host, tiny_port);

    const std::string& data_host = sNetworkConfig.server_host();
    short              data_port = sNetworkConfig.server_port();

    LOG_INFO("Connecting to DataServer: {}:{}...", data_host, data_port);
    bool data_result = data_client_->connect(data_host, data_port);

    if (!tiny_result || !data_result)
    {
        LOG_ERROR("Failed to initiate backend connections");
        return false;
    }

    return true;
}

void GatewayServer::onTinyClientConnected(bool success, const std::string& error)
{
    if (!tiny_client_)
        return;

    if (success)
    {
        tiny_client_->setConnectionState(cncpp::TcpClient::ConnectionState::Connected);
        LOG_INFO("TinyClient connected to TinyServer successfully");
    }
    else
    {
        tiny_client_->setConnectionState(cncpp::TcpClient::ConnectionState::Disconnected);
        LOG_ERROR("TinyClient failed to connect to TinyServer: {}", error);
    }
}

void GatewayServer::onTinyClientDisconnected()
{
    if (!tiny_client_)
        return;

    tiny_client_->setConnectionState(cncpp::TcpClient::ConnectionState::Disconnected);
    LOG_WARN("TinyClient disconnected from TinyServer");
}

void GatewayServer::onTinyClientMessage(const cncpp::NetworkMessage& message)
{
    LOG_DEBUG("TinyClient received message: msg_id={}", message.header_.message_id_);
}

void GatewayServer::onDataClientConnected(bool success, const std::string& error)
{
    if (!data_client_)
        return;

    if (success)
    {
        data_client_->setConnectionState(cncpp::TcpClient::ConnectionState::Connected);
        LOG_INFO("DataClient connected to DataServer successfully");
    }
    else
    {
        data_client_->setConnectionState(cncpp::TcpClient::ConnectionState::Disconnected);
        LOG_ERROR("DataClient failed to connect to DataServer: {}", error);
    }
}

void GatewayServer::onDataClientDisconnected()
{
    if (!tiny_client_)
        return;

    data_client_->setConnectionState(cncpp::TcpClient::ConnectionState::Disconnected);
    LOG_WARN("DataClient disconnected from DataServer");
}

void GatewayServer::onDataClientMessage(const cncpp::NetworkMessage& message)
{
    LOG_DEBUG("DataClient received message: msg_id={}", message.header_.message_id_);
}

bool GatewayServer::checkBackendReady()
{
    bool tiny_ready = tiny_client_
                      && (tiny_client_->getConnectionState() == cncpp::TcpClient::ConnectionState::Connected
                          || tiny_client_->getConnectionState() == cncpp::TcpClient::ConnectionState::Okay);

    bool data_ready = data_client_
                      && (data_client_->getConnectionState() == cncpp::TcpClient::ConnectionState::Connected
                          || data_client_->getConnectionState() == cncpp::TcpClient::ConnectionState::Okay);

    return tiny_ready && data_ready;
}

void GatewayServer::stopAcceptor()
{
    if (acceptor_)
    {
        acceptor_->stop();
        acceptor_.reset();
        LOG_INFO("Acceptor stopped");
    }
}

bool GatewayServer::initAcceptor()
{
    if (acceptor_)
        return false;

    acceptor_ = sIOContextPool.createAcceptor(
        sNetworkConfig.port(), std::bind(&GatewayServer::onClientConnected, this, std::placeholders::_1));

    if (!acceptor_)
    {
        LOG_ERROR("Failed to create acceptor");
        return false;
    }

    LOG_INFO("Acceptor initialized on port {}", sNetworkConfig.port());
    return true;
}

bool GatewayServer::startAcceptor()
{
    if (!acceptor_)
        return false;

    acceptor_->start();
    LOG_INFO("Acceptor started on port {}", sNetworkConfig.port());
    return true;
}

void GatewayServer::closeAllSessions()
{
    sGateTaskManager.stopTaskScheduler();
    LOG_INFO("All tasks stopped");
}

void GatewayServer::finalAll()
{
}

void GatewayServer::onClientConnected(tcp::socket&& sock)
{
    auto task = sGateTaskManager.addTask(std::move(sock), "gateway_001");

    if (task)
    {
        task->start();
        LOG_INFO("New connection created, task_id: {}, client_ip: {}", task->getTaskID(), task->getClientIP());
    }
}

bool GatewayServer::onTick()
{
    static uint32_t tick_count = 0;

    if (++tick_count % 100 == 0)
    {
        sGateTaskManager.cleanupTimeoutTasks();
        sGateUserManager.cleanupTimeoutUsers();

        bool tiny_connected = tiny_client_ && tiny_client_->isConnected();
        bool data_connected = data_client_ && data_client_->isConnected();

        LOG_DEBUG("Gateway tick - active tasks: {}, online users: {}, tiny_connected: {}, data_connected: {}",
                  sGateTaskManager.getStats().active_tasks, sGateUserManager.getOnlineUserCount(), tiny_connected,
                  data_connected);
    }

    return true;
}

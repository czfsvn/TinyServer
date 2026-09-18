#include "gateway_server.h"
#include "Misc.h"
#include "gate_task_manager.h"
#include "gate_user_manager.h"
#include "io_context_pool.h"
#include "logger.h"
#include "stacktrace.h"
#include "xmlloader.h"

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

    LOG_INFO("GatewayServer initialized successfully");
    return true;
}

bool GatewayServer::onStart()
{
    LOG_INFO("GatewayServer starting...");

    sGateTaskManager.startTaskScheduler();

    if (!connectToBackendServers())
    {
        LOG_ERROR("Failed to connect to backend servers");
        return false;
    }

    LOG_ERROR("[GatewayServer] stack: {}", cncpp::captureStackTrace(1, 5));

    const uint32_t max_retry_count = 5;
    uint32_t       retry_count     = 0;
    bool           is_ready        = false;
    while (retry_count < max_retry_count)
    {
        is_ready = checkBackendReady();
        if (is_ready)
        {
            break;
        }

        cncpp::sleepfor_seconds(2);
        retry_count++;
    }

    LOG_STACK(DEBUG);

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

        //tiny_client_->setConnectCallback(
        //    std::bind(&GatewayServer::onTinyClientConnected, this, std::placeholders::_1, std::placeholders::_2));
        //tiny_client_->setMessageCallback(std::bind(&GatewayServer::onTinyClientMessage, this, std::placeholders::_1));
        //tiny_client_->setDisconnectCallback(std::bind(&GatewayServer::onTinyClientDisconnected, this));

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
    return conectTinyServer() && connectToDataServer();
}

bool GatewayServer::conectTinyServer()
{
    const std::string& tiny_host = sTinyServerConfig.listen_host();
    const short        tiny_port = sTinyServerConfig.listen_port();

    LOG_INFO("Connecting to TinyServer: {}:{}...", tiny_host, tiny_port);
    return tiny_client_->connect(tiny_host, tiny_port);
}

bool GatewayServer::connectToDataServer()
{
    return true;
#if 0
    const std::string& data_host = sDataServerConfig.listen_host();
    const short        data_port = sDataServerConfig.listen_port();

    LOG_INFO("Connecting to DataServer: {}:{}...", data_host, data_port);
    return data_client_->connect(data_host, data_port);
#endif
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
    return checkTinyClientReady() && checkDataClientReady();
}

bool GatewayServer::checkTinyClientReady()
{
    if (!tiny_client_)
        return false;

    return (tiny_client_->getConnectionState() == cncpp::TcpClient::ConnectionState::Connected
            || tiny_client_->getConnectionState() == cncpp::TcpClient::ConnectionState::Okay);
}

bool GatewayServer::checkDataClientReady()
{
    return true;
#if 0
    if (!data_client_)
        return false;

    return (data_client_->getConnectionState() == cncpp::TcpClient::ConnectionState::Connected
            || data_client_->getConnectionState() == cncpp::TcpClient::ConnectionState::Okay);
#endif
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
        sGatewayConfig.listen_port(), std::bind(&GatewayServer::onClientConnected, this, std::placeholders::_1));

    if (!acceptor_)
    {
        LOG_ERROR("Failed to create acceptor");
        return false;
    }

    return true;
}

bool GatewayServer::startAcceptor()
{
    if (!acceptor_)
        return false;

    acceptor_->start();
    LOG_INFO("Acceptor started on port {}", sGatewayConfig.listen_port());
    return true;
}

void GatewayServer::closeAllSessions()
{
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

    // 本拍到达的消息全部在这一拍交给业务。业务与定时器回调共享"无需加锁"
    // 这个前提，前提成立靠的就是它们在同一拍内串行发生（ADR-0004）。
    drainInboundMessages();

    if (++tick_count % 100 == 0)
    {
        sGateTaskManager.cleanupTimeoutTasks();
        sGateUserManager.cleanupTimeoutUsers();

        //bool tiny_connected = tiny_client_ && tiny_client_->isConnected();
        //bool data_connected = data_client_ && data_client_->isConnected();

        //LOG_DEBUG("Gateway tick - active tasks: {}, online users: {}, tiny_connected: {}, data_connected: {}",
        //          sGateTaskManager.getStats().active_tasks, sGateUserManager.getOnlineUserCount(), tiny_connected,
        //          data_connected);
    }

    return true;
}

void GatewayServer::drainInboundMessages()
{
    // getAllTasks() 只在拷贝任务表那一小段持锁。分发时绝不能握着这把锁：
    // io 线程 accept 到新连接要在 addTask 上拿同一把锁，一次慢分发会把它挡在门外。
    const std::vector<GateTaskPtr> tasks = sGateTaskManager.getAllTasks();

    size_t message_count = 0;
    for (const auto& task : tasks)
    {
        // 任务 stop() 之后 session_ 会被置空，而这是一拍开头拿的快照。
        const std::shared_ptr<cncpp::Session> session = task->getSession();
        if (!session)
        {
            continue;
        }

        // 抽干。会话已关闭也要照做：close() 之后队列里已解码的帧仍然要交给业务（A6）。
        cncpp::NetworkMessage message;
        while (session->getReceiveQueue().pop(message))
        {
            // 交给任务自己分发：PENDING/INITIALIZING 走认证、EXECUTING 走业务，
            // 这个状态机只有 GateTask 知道，同一个连接上两类帧的先后顺序也由它保证。
            task->processMessage(message);
            ++message_count;
        }
    }

    if (message_count > 0)
    {
        LOG_DEBUG("GatewayServer tick dispatched {} messages from {} tasks", message_count, tasks.size());
    }
}

void GatewayServer::loadGameConfigs()
{
    xmlconfigs::gateway_loadConfig();
}

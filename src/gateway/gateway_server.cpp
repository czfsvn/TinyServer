#include "gateway_server.h"
#include "io_context_pool.h"
#include "logger.h"
#include "stringutil.h"

GatewayServer::GatewayServer()
{
    // 生成唯一网关ID
    gateway_id_ = "gateway_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

    // 创建管理器
    task_manager_ = std::make_shared<GateTaskManager>();
    user_manager_ = std::make_shared<GateUserManager>();

    // 创建网关客户端（用于连接 tinyserver）
    server_client_ = std::make_shared<GatewayClient>();
}

GatewayServer::~GatewayServer()
{
    Stop();
}

bool GatewayServer::Start()
{
    if (running_)
    {
        LOG_WARN("GatewayServer is already running");
        return true;
    }

    try
    {
        // 启动客户端监听
        uint16_t client_port = sNetworkConfig.port();
        LOG_INFO("Starting GatewayServer, listening on port {}", client_port);

        acceptor_ = sIOContextPool.createAcceptor(
            client_port, std::bind(&GatewayServer::onClientConnected, this, std::placeholders::_1));

        if (!acceptor_)
        {
            LOG_ERROR("Failed to create acceptor");
            return false;
        }

        // 连接到 tinyserver
        if (!connectToTinyServer())
        {
            LOG_ERROR("Failed to connect to tinyserver");
            return false;
        }

        running_ = true;
        LOG_INFO("GatewayServer started successfully, gateway_id: {}", gateway_id_);
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to start GatewayServer: {}", e.what());
        return false;
    }
}

void GatewayServer::Stop()
{
    if (!running_)
    {
        return;
    }

    LOG_INFO("Stopping GatewayServer...");

    // 停止客户端监听
    if (acceptor_)
    {
        acceptor_->Stop();
        acceptor_.reset();
    }

    // 断开与 tinyserver 的连接
    if (server_client_)
    {
        server_client_->disconnect();
        server_client_.reset();
    }

    // 停止所有任务
    if (task_manager_)
    {
        task_manager_->stopAllTasks();
    }

    running_ = false;
    LOG_INFO("GatewayServer stopped");
}

void GatewayServer::WaitForStop()
{
    sIOContextPool.WaitForStop();
}

void GatewayServer::processMessages()
{
    // 清理超时任务和用户
    if (task_manager_)
    {
        task_manager_->cleanupTimeoutTasks();
    }

    if (user_manager_)
    {
        user_manager_->cleanupTimeoutUsers();
    }

    LOG_DEBUG("GatewayServer processMessages - active tasks: {}, active users: {}",
              task_manager_ ? task_manager_->getActiveTaskCount() : 0,
              user_manager_ ? user_manager_->getOnlineUserCount() : 0);
}

void GatewayServer::onClientConnected(tcp::socket&& socket)
{
    try
    {
        // 创建任务
        auto task = task_manager_->addTask(std::move(socket), gateway_id_);

        // 启动任务
        task->start();

        LOG_INFO("New client connected, task_id: {}", task->getTaskID());
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to handle client connection: {}", e.what());
    }
}

void GatewayServer::onTaskMessage(GateTaskPtr task, const NetworkMessage& message)
{
    // 更新最后活跃时间
    task->updateLastActiveTime();

    // 获取或创建用户
    auto user = task->getUser();
    if (!user)
    {
        // 尝试从消息中获取用户ID进行认证
        if (message.header_.msg_type_ == MessageType::AUTH)
        {
            try
            {
                uint32_t user_id = std::stoul(message.body_);

                // 创建或获取用户
                user = user_manager_->addUser(user_id);
                task->setUser(user);

                // 更新用户到任务的映射
                LOG_INFO("User authenticated: {}", user_id);
            }
            catch (const std::exception& e)
            {
                LOG_WARN("Failed to parse user_id: {}", e.what());
            }
        }
    }

    // 如果已认证，转发消息到服务器
    if (user)
    {
        forwardToServer(user->getUserID(), message);
    }
}

void GatewayServer::onTaskDisconnected(GateTaskPtr task)
{
    LOG_INFO("Client disconnected, task_id: {}", task->getTaskID());

    // 更新用户状态
    auto user = task->getUser();
    if (user)
    {
        user->setState(UserState::Offline);
        LOG_INFO("User disconnected: {}", user->getUserID());
    }

    // 移除任务
    task_manager_->removeTask(task->getTaskID());

    // 通知服务器用户断开
    if (user && server_client_ && server_client_->isConnected())
    {
        NetworkMessage msg;
        msg.header_.msg_type_ = MessageType::DISCONNECT;
        msg.header_.from_     = std::to_string(user->getUserID());
        msg.header_.to_       = "server";
        server_client_->sendMessage(msg);
    }
}

void GatewayServer::onServerConnected(bool success, const std::string& error_msg)
{
    if (success)
    {
        LOG_INFO("Connected to tinyserver successfully");

        // 发送网关注册消息
        NetworkMessage register_msg;
        register_msg.header_.msg_type_ = MessageType::REGISTER;
        register_msg.header_.from_     = gateway_id_;
        register_msg.header_.to_       = "server";
        register_msg.body_             = "gateway_register";
        server_client_->sendMessage(register_msg);
    }
    else
    {
        LOG_ERROR("Failed to connect to tinyserver: {}", error_msg);
    }
}

bool GatewayServer::connectToTinyServer()
{
    try
    {
        std::string server_host = sNetworkConfig.server_host();
        uint16_t    server_port = sNetworkConfig.server_port();

        LOG_INFO("Connecting to tinyserver at {}:{}", server_host, server_port);

        // 发起连接
        return server_client_->connect(server_host, server_port);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to connect to tinyserver: {}", e.what());
        return false;
    }
}

void GatewayServer::onServerMessage(const NetworkMessage& message)
{
    LOG_DEBUG("Received message from tinyserver: {}", message.body_);

    // 根据目标用户转发消息
    if (!message.header_.to_.empty() && message.header_.to_ != "server")
    {
        try
        {
            uint32_t user_id = std::stoul(message.header_.to_);
            forwardToClient(user_id, message);
        }
        catch (const std::exception& e)
        {
            LOG_WARN("Failed to parse user_id from message: {}", e.what());
        }
    }
}

void GatewayServer::forwardToServer(uint32_t user_id, const NetworkMessage& message)
{
    if (!server_client_ || !server_client_->isConnected())
    {
        LOG_WARN("Not connected to tinyserver, cannot forward message");
        return;
    }

    NetworkMessage forward_msg = message;
    forward_msg.header_.from_  = std::to_string(user_id);
    forward_msg.header_.to_    = "server";

    server_client_->sendMessage(forward_msg);
    LOG_DEBUG("Forwarded message from {} to server", user_id);
}

void GatewayServer::forwardToClient(uint32_t user_id, const NetworkMessage& message)
{
    if (!task_manager_)
    {
        LOG_WARN("Task manager not initialized");
        return;
    }

    bool sent = task_manager_->sendMessageToUser(user_id, message);
    if (!sent)
    {
        LOG_WARN("Failed to forward message to user {}", user_id);
    }
}
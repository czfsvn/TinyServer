#include "tinyserver.h"
#include "Global.h"
#include "Misc.h"
#include "io_context_pool.h"
#include "logger.h"
#include "session_manager.h"
#include "timer_wheel.h"
#include "tiny_task_manager.h"

// 全局会话管理器（保留兼容旧代码）
cncpp::SessionManager<cncpp::Session> g_session_manager;

TinyServer::TinyServer()
{
}

TinyServer::~TinyServer()
{
}

bool TinyServer::onInit()
{
    if (!initGame())
    {
        LOG_ERROR("Failed to init game");
        return false;
    }

    if (!initAcceptor())
    {
        LOG_ERROR("Failed to init acceptor");
        return false;
    }

    LOG_INFO("TinyServer started successfully");
    return true;
}

bool TinyServer::onStart()
{
    if (!startAcceptor())
    {
        LOG_ERROR("Failed to start acceptor");
        return false;
    }

    LOG_INFO("TinyServer started successfully");
    return true;
}

void TinyServer::onStop()
{
    stopAcceptor();
    closeAllSessions();
    finalAll();
}

void TinyServer::stopAcceptor()
{
    if (acceptor_)
    {
        acceptor_->stop();
        acceptor_.reset();
        LOG_INFO("Acceptor stopped");
    }
}

bool TinyServer::initAcceptor()
{
    if (acceptor_)
        return false;

    acceptor_ = sIOContextPool.createAcceptor(sNetworkConfig.port(),
                                              std::bind(&TinyServer::onConnectionCreated, this, std::placeholders::_1));
    if (!acceptor_)
    {
        LOG_ERROR("Failed to create acceptor");
        return false;
    }

    return true;
}

bool TinyServer::startAcceptor()
{
    if (!acceptor_)
        return false;

    acceptor_->start();
    return true;
}

void TinyServer::closeAllSessions()
{
    // 使用单例任务管理器停止所有任务
    sTinyTaskManager.stopAllTasks();
    LOG_INFO("All tasks stopped");

    // 停止所有会话（保留兼容旧代码）
    g_session_manager.CloseAllSessions();
    LOG_INFO("All sessions closed");
}

void TinyServer::finalAll()
{
    // 清理资源
    // ...
}

void TinyServer::onConnectionCreated(tcp::socket&& sock)
{
    // 使用单例任务管理器创建新任务
    auto task = sTinyTaskManager.addTask(std::move(sock));

    if (task)
    {
        // 启动任务
        task->start();

        LOG_INFO("New connection created, task_id: {}, client_ip: {}", task->getTaskID(), task->getClientIP());
    }
}

void TinyServer::onMessageReceived(const cncpp::NetworkMessage& message, const std::string& session_info)
{
    LOG_INFO("Received message: {} from {}", message.body_, session_info);
}

bool TinyServer::initGame()
{
    // add other init code
    // ...
    return true;
}

bool TinyServer::onTick()
{
    gameUpdate();

    // message process tick
    {
        g_session_manager.ProcessAllSessionQueues<cncpp::NetworkMessage>(
            [](const cncpp::NetworkMessage& message, const std::string& session_info) {
            // 处理消息
            LOG_INFO("Received message: {} from {}", message.body_, session_info);
        });
        // std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 使用单例任务管理器遍历任务（示例）
    size_t active_count = sTinyTaskManager.getActiveTaskCount();
    LOG_DEBUG("Active tasks: {}", active_count);

    return true;
}

void TinyServer::gameUpdate()
{
    // 其他定时器tick
}
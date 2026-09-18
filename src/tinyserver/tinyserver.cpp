#include "tinyserver.h"
#include "Global.h"
#include "Misc.h"
#include "io_context_pool.h"
#include "logger.h"
#include "timer_wheel.h"
#include "tiny_task_manager.h"

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

    acceptor_ = sIOContextPool.createAcceptor(sTinyServerConfig.listen_port(),
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
    LOG_INFO("Acceptor started on port {}", sTinyServerConfig.listen_port());
    return true;
}

void TinyServer::closeAllSessions()
{
    // stopAllTasks() 会 stop 每个任务，TcpTask::stop() 负责关闭它持有的会话，
    // 所以这里不必另外再走一遍会话列表。
    sTinyTaskManager.stopAllTasks();
    LOG_INFO("All tasks stopped, all sessions closed");
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
    // 每条入站消息打一行 INFO 会在正常负载下把日志淹掉。这里只是占位：
    // 真正的业务分发要等 TinyServer 这边有 handler 才有意义。
    LOG_DEBUG("Received message id={} from {}", message.header_.message_id_, session_info);
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

    // 本拍到达的消息全部在这一拍交给业务。业务与定时器回调共享"无需加锁"
    // 这个前提，前提成立靠的就是它们在同一拍内串行发生（ADR-0004）。
    drainInboundMessages();

    return true;
}

void TinyServer::drainInboundMessages()
{
    // getAllTasks() 只在拷贝任务表那一小段持锁。分发时绝不能握着这把锁：
    // io 线程 accept 到新连接要在 addTask 上拿同一把锁，一次慢分发会把它挡在门外。
    const std::vector<TinyTaskPtr> tasks = sTinyTaskManager.getAllTasks();

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
            onMessageReceived(message, task->getClientIP());
            ++message_count;
        }
    }

    if (message_count > 0)
    {
        LOG_DEBUG("TinyServer tick dispatched {} messages from {} tasks", message_count, tasks.size());
    }
}

void TinyServer::gameUpdate()
{
    // 其他定时器tick
}
#include "tiny_task.h"
#include "logger.h"
#include "tiny_task_manager.h"

TinyTask::TinyTask(tcp::socket&& socket) : cncpp::TcpTask(std::move(socket))
{
    LOG_DEBUG("TinyTask created, task_id: {}", getTaskID());
}

TinyTask::~TinyTask()
{
}

void TinyTask::setClientID(uint32_t client_id)
{
    // 如果之前有旧的 client_id，先移除旧映射
    if (client_id_ != 0)
    {
        sTinyTaskManager.removeClientMapping(client_id_);
    }

    client_id_ = client_id;

    // 如果新的 client_id 不为空（不为0），更新映射
    if (client_id != 0)
    {
        sTinyTaskManager.updateClientMapping(client_id, std::static_pointer_cast<TinyTask>(shared_from_this()));
    }

    LOG_INFO("TinyTask {} set client_id: {}", getTaskID(), client_id);
}

void TinyTask::processMessage(const cncpp::NetworkMessage& message)
{
    LOG_DEBUG("TinyTask {} received message: {}", getTaskID(), message.body_);

    // 根据任务状态处理消息
    cncpp::TcpTaskState current_state = getState();
    switch (current_state)
    {
        case cncpp::TcpTaskState::Connected:
        case cncpp::TcpTaskState::Ready:
        case cncpp::TcpTaskState::Processing:
            // 处理业务消息
            setState(cncpp::TcpTaskState::Processing);

            // 在这里添加具体的业务逻辑处理

            setState(cncpp::TcpTaskState::Ready);
            break;

        default:
            LOG_WARN("Received message in invalid state: {}", static_cast<int>(current_state));
            break;
    }
}

void TinyTask::onConnected()
{
    // 调用父类方法
    cncpp::TcpTask::onConnected();

    LOG_INFO("TinyTask client connected: {}", getClientIP());
}

void TinyTask::onMessage(const cncpp::NetworkMessage& message)
{
    // 调用父类方法
    cncpp::TcpTask::onMessage(message);

    // TinyServer 特定的消息处理
    processMessage(message);
}

void TinyTask::onDisconnected()
{
    LOG_INFO("TinyTask client disconnected: {}", getClientIP());

    // 调用父类方法
    cncpp::TcpTask::onDisconnected();
}
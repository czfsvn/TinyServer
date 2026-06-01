#include "tiny_task.h"
#include "logger.h"

TinyTask::TinyTask(tcp::socket&& socket) : cncpp::TcpTask(std::move(socket))
{
    LOG_DEBUG("TinyTask created, task_id: {}", getTaskID());
}

TinyTask::~TinyTask()
{
}

void TinyTask::processMessage(const cncpp::NetworkMessage& message)
{
    LOG_DEBUG("TinyTask {} received message", getTaskID());

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
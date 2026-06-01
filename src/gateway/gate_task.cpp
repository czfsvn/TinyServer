#include "gate_task.h"
#include "gate_user_manager.h"
#include "gateway_server.h"
#include "logger.h"
#include "message_ids.h"

GateTask::GateTask(tcp::socket&& socket, const std::string& gateway_id)
    : cncpp::TcpTask(std::move(socket)),
      gateway_id_(std::stoul(gateway_id)),
      status_(TaskStatus::PENDING),
      priority_(TaskPriority::NORMAL),
      create_time_(std::chrono::steady_clock::now()),
      timeout_ms_(300000)
{
    LOG_DEBUG("GateTask created, task_id: {}, gateway_id: {}", getTaskID(), gateway_id_);
}

GateTask::~GateTask()
{
    LOG_DEBUG("GateTask destroyed, task_id: {}", getTaskID());
}

GateUserPtr GateTask::getUser() const
{
    return user_;
}

void GateTask::setUser(GateUserPtr user)
{
    user_ = user;
    if (user)
    {
        user_->setGatewayID(std::to_string(gateway_id_));
        user_->setIPAddress(getClientIP());
        user_->setConnectTime();
        user_->setState(UserState::Connected);
    }
}

void GateTask::setStatus(TaskStatus status)
{
    TaskStatus old_status = status_.exchange(status);

    if (status == TaskStatus::EXECUTING)
    {
        start_time_ = std::chrono::steady_clock::now();
    }
    else if (status == TaskStatus::COMPLETED || status == TaskStatus::FAILED || status == TaskStatus::TIMEOUT
             || status == TaskStatus::CANCELLED)
    {
        end_time_ = std::chrono::steady_clock::now();
    }

    LOG_DEBUG("Task {} status changed: {} -> {}", getTaskID(), static_cast<int>(old_status), static_cast<int>(status));
}

bool GateTask::isTimeout() const
{
    if (status_.load() != TaskStatus::EXECUTING)
        return false;

    auto now     = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
    return elapsed.count() > timeout_ms_;
}

void GateTask::startTask()
{
    setStatus(TaskStatus::EXECUTING);
    LOG_INFO("Task {} started", getTaskID());
}

void GateTask::completeTask()
{
    setStatus(TaskStatus::COMPLETED);
    LOG_INFO("Task {} completed", getTaskID());
}

void GateTask::failTask(const std::string& error)
{
    last_error_ = error;
    setStatus(TaskStatus::FAILED);
    LOG_ERROR("Task {} failed: {}", getTaskID(), error);
}

void GateTask::cancelTask()
{
    setStatus(TaskStatus::CANCELLED);
    LOG_INFO("Task {} cancelled", getTaskID());
}

void GateTask::processMessage(const cncpp::NetworkMessage& message)
{
    LOG_DEBUG("GateTask {} received message: {}", getTaskID(), message.header_.message_id_);

    TaskStatus current_status = status_.load();

    switch (current_status)
    {
        case TaskStatus::PENDING:
        case TaskStatus::INITIALIZING:
            if (processAuthentication(message))
            {
                startTask();
            }
            break;

        case TaskStatus::EXECUTING:
            processBusinessMessage(message);
            break;

        default:
            LOG_WARN("Received message in invalid state: {}", static_cast<int>(current_status));
            break;
    }
}

void GateTask::onConnected()
{
    cncpp::TcpTask::onConnected();
    setStatus(TaskStatus::INITIALIZING);
    LOG_INFO("GateTask client connected: {}, task_id: {}", getClientIP(), getTaskID());
}

void GateTask::onMessage(const cncpp::NetworkMessage& message)
{
    cncpp::TcpTask::onMessage(message);
    processMessage(message);
}

void GateTask::onDisconnected()
{
    LOG_INFO("GateTask client disconnected: {}, task_id: {}", getClientIP(), getTaskID());

    auto current_status = status_.load();
    if (current_status != TaskStatus::COMPLETED && current_status != TaskStatus::CANCELLED)
    {
        failTask("Client disconnected");
    }

    if (user_)
    {
        user_->setState(UserState::Offline);
    }

    cncpp::TcpTask::onDisconnected();
}

bool GateTask::processAuthentication(const cncpp::NetworkMessage& message)
{
    if (message.header_.message_id_ == toUint32(MessageId::AUTH))
    {
        try
        {
            uint32_t user_id = 0;

            auto user = sGateUserManager.addUser(user_id);
            setUser(user);
            user->setState(UserState::Authenticated);

            LOG_INFO("User authenticated: {}", user_id);
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_WARN("Failed to parse user_id: {}", e.what());
        }
    }

    LOG_WARN("Failed to authenticate client: {}", getClientIP());
    return false;
}

void GateTask::processBusinessMessage(const cncpp::NetworkMessage& message)
{
    if (user_)
    {
        user_->incrementMessageCount();
        user_->updateLastActiveTime();
        LOG_DEBUG("Processing message from user {}: {}", user_->getUserID(), message.header_.message_id_);
    }

    auto tiny_client = sGatewayServer.getTinyClient();
    if (tiny_client && tiny_client->isConnected())
    {
        tiny_client->sendUserMessage(message);
        LOG_DEBUG("Forwarded message to TinyServer, task_id: {}", getTaskID());
    }
    else
    {
        LOG_ERROR("TinyClient not available for task {}", getTaskID());
    }
}

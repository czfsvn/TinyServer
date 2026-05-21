#include "gate_task.h"
#include "logger.h"

GateTask::GateTask(tcp::socket&& socket, const std::string& gateway_id)
    : cncpp::TcpTask(std::move(socket)), gateway_id_(gateway_id)
{
    LOG_DEBUG("GateTask created, task_id: {}, gateway_id: {}", getTaskID(), gateway_id_);
}

GateTask::~GateTask()
{
}

void GateTask::setUser(GateUserPtr user)
{
    user_ = user;
    if (user)
    {
        user_->setGatewayID(gateway_id_);
        user_->setIPAddress(getClientIP());
        user_->setConnectTime();
        user_->setState(UserState::Connected);
    }
}

void GateTask::processMessage(const NetworkMessage& message)
{
    LOG_DEBUG("GateTask {} received message: {}", getTaskID(), message.body_);

    // 根据任务状态处理消息
    cncpp::TcpTaskState current_state = getState();
    switch (current_state)
    {
        case cncpp::TcpTaskState::Connected:
            // 未认证状态，尝试认证
            if (processAuthentication(message))
            {
                setState(cncpp::TcpTaskState::Ready);
                if (user_)
                {
                    user_->setState(UserState::Online);
                }
            }
            break;

        case cncpp::TcpTaskState::Ready:
        case cncpp::TcpTaskState::Processing:
            // 已认证状态，处理业务消息
            setState(cncpp::TcpTaskState::Processing);
            processBusinessMessage(message);
            setState(cncpp::TcpTaskState::Ready);
            break;

        default:
            LOG_WARN("Received message in invalid state: {}", static_cast<int>(current_state));
            break;
    }
}

void GateTask::onConnected()
{
    // 调用父类方法
    cncpp::TcpTask::onConnected();

    LOG_INFO("GateTask client connected: {}", getClientIP());
}

void GateTask::onMessage(const NetworkMessage& message)
{
    // 调用父类方法
    cncpp::TcpTask::onMessage(message);

    // 网关特定的消息处理
    processMessage(message);
}

void GateTask::onDisconnected()
{
    LOG_INFO("GateTask client disconnected: {}", getClientIP());

    if (user_)
    {
        user_->setState(UserState::Offline);
    }

    // 调用父类方法
    cncpp::TcpTask::onDisconnected();
}

bool GateTask::processAuthentication(const NetworkMessage& message)
{
    // 简单的认证逻辑：检查消息中是否包含 user_id（uint32_t）
    // 实际应用中应该使用更安全的认证机制

    if (message.header_.msg_type_ == MessageType::AUTH)
    {
        try
        {
            // 解析用户ID（从消息体中提取，期望是数字字符串）
            uint32_t user_id = std::stoul(message.body_);

            // 创建或获取用户
            user_ = std::make_shared<GateUser>(user_id);
            user_->setGatewayID(gateway_id_);
            user_->setIPAddress(getClientIP());
            user_->setConnectTime();
            user_->setState(UserState::Authenticated);

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

void GateTask::processBusinessMessage(const NetworkMessage& message)
{
    // 业务消息处理逻辑
    // 这里可以添加具体的业务逻辑处理

    if (user_)
    {
        user_->incrementMessageCount();
        LOG_DEBUG("Processing message from user {}: {}", user_->getUserID(), message.body_);
    }
}
#include "tcp_task.h"
#include "logger.h"

namespace cncpp
{

    std::atomic<uint32_t> TcpTask::task_id_counter_{1};

    TcpTask::TcpTask(tcp::socket&& socket)
        : task_id_(task_id_counter_.fetch_add(1, std::memory_order_relaxed)),
          connect_time_(std::chrono::steady_clock::now()),
          last_active_time_(connect_time_)
    {
        // 获取客户端IP地址
        try
        {
            boost::asio::ip::tcp::endpoint remote_endpoint = socket.remote_endpoint();
            client_ip_                                     = remote_endpoint.address().to_string();
        }
        catch (const std::exception&)
        {
            client_ip_ = "unknown";
        }

        // 创建会话
        session_ = std::make_shared<Session>(std::move(socket));

        LOG_DEBUG("TcpTask created, task_id: {}, client_ip: {}", task_id_, client_ip_);
    }

    TcpTask::~TcpTask()
    {
        stop();
    }

    void TcpTask::start()
    {
        if (state_.load() != TcpTaskState::Idle)
        {
            LOG_WARN("TcpTask already started, task_id: {}", task_id_);
            return;
        }

        setState(TcpTaskState::Connecting);

        // 启动会话
        session_->start();

        setState(TcpTaskState::Connected);

        // 调用连接成功钩子
        onConnected();

        LOG_INFO("TcpTask started, task_id: {}, client_ip: {}", task_id_, client_ip_);
    }

    void TcpTask::stop()
    {
        TcpTaskState current = state_.exchange(TcpTaskState::Disconnecting);
        if (current == TcpTaskState::Disconnected || current == TcpTaskState::Disconnecting)
        {
            return;
        }

        LOG_INFO("Stopping TcpTask, task_id: {}", task_id_);

        if (session_)
        {
            session_->close();
            session_.reset();
        }

        setState(TcpTaskState::Disconnected);

        LOG_INFO("TcpTask stopped, task_id: {}", task_id_);
    }

    TcpTaskState TcpTask::getState() const
    {
        return state_.load();
    }

    bool TcpTask::sendMessage(const cncpp::NetworkMessage& message)
    {
        if (!session_ || !session_->isOpen())
        {
            LOG_ERROR("TcpTask not connected, cannot send message, task_id: {}", task_id_);
            return false;
        }

        try
        {
            // todo: 发送消息到会话
            // session_->send(message);
            updateLastActiveTime();

            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("TcpTask failed to send message, task_id: {}, error: {}", task_id_, e.what());
            return false;
        }
    }

    bool TcpTask::isActive() const
    {
        TcpTaskState current = state_.load();
        return current == TcpTaskState::Connected || current == TcpTaskState::Authenticating
               || current == TcpTaskState::Ready || current == TcpTaskState::Processing;
    }

    void TcpTask::updateLastActiveTime()
    {
        last_active_time_ = std::chrono::steady_clock::now();
    }

    void TcpTask::setMessageCallback(std::function<void(const NetworkMessage&)> callback)
    {
        msg_callback_ = std::move(callback);
    }

    void TcpTask::setCloseCallback(std::function<void()> callback)
    {
        close_callback_ = std::move(callback);
    }

    void TcpTask::onConnected()
    {
        // 默认实现为空，子类可重写
    }

    void TcpTask::onMessage(const NetworkMessage& /*message*/)
    {
        // 默认实现为空，子类可重写
    }

    void TcpTask::onDisconnected()
    {
        // 默认实现为空，子类可重写
    }

    void TcpTask::setState(TcpTaskState new_state)
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        TcpTaskState                old_state = state_.load();

        if (old_state == new_state)
            return;

        LOG_DEBUG("TcpTask state changed, task_id: {}, {} -> {}", task_id_, stateToString(old_state),
                  stateToString(new_state));

        state_.store(new_state);
    }

    std::string TcpTask::stateToString(TcpTaskState state)
    {
        switch (state)
        {
            case TcpTaskState::Idle:
                return "Idle";
            case TcpTaskState::Connecting:
                return "Connecting";
            case TcpTaskState::Connected:
                return "Connected";
            case TcpTaskState::Authenticating:
                return "Authenticating";
            case TcpTaskState::Ready:
                return "Ready";
            case TcpTaskState::Processing:
                return "Processing";
            case TcpTaskState::Disconnecting:
                return "Disconnecting";
            case TcpTaskState::Disconnected:
                return "Disconnected";
            default:
                return "Unknown";
        }
    }

    void TcpTask::handleMessage(const cncpp::NetworkMessage& message)
    {
        updateLastActiveTime();

        // 调用消息钩子
        onMessage(message);

        // 调用消息回调
        if (msg_callback_)
        {
            try
            {
                msg_callback_(message);
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Error handling message in TcpTask, task_id: {}, error: {}", task_id_, e.what());
            }
        }
    }

    void TcpTask::handleClose()
    {
        LOG_WARN("TcpTask connection closed, task_id: {}", task_id_);

        // 调用断开钩子
        onDisconnected();

        // 调用断开回调
        if (close_callback_)
        {
            try
            {
                close_callback_();
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Error handling close in TcpTask, task_id: {}, error: {}", task_id_, e.what());
            }
        }

        // 更新状态
        setState(TcpTaskState::Disconnected);
    }

}  // namespace cncpp
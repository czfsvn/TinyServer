#include "tcp_client.h"
#include "io_context_pool.h"
#include "logger.h"

namespace cncpp
{

    TcpClient::TcpClient()
    {
    }

    TcpClient::~TcpClient()
    {
        disconnect();
    }

    void TcpClient::setMessageCallback(MessageCallback callback)
    {
        msg_callback_ = std::move(callback);
    }

    void TcpClient::setConnectCallback(ConnectCallback callback)
    {
        connect_callback_ = std::move(callback);
    }

    void TcpClient::setDisconnectCallback(DisconnectCallback callback)
    {
        disconnect_callback_ = std::move(callback);
    }

    bool TcpClient::connect(const std::string& host, short port)
    {
        ConnectionState current = connection_state_.load();
        if (current == ConnectionState::Connected)
        {
            LOG_WARN("TcpClient already connected to {}:{}", host_, port_);
            return false;
        }

        if (current == ConnectionState::Connecting)
        {
            LOG_WARN("TcpClient is already connecting");
            return false;
        }

        host_ = host;
        port_ = port;

        try
        {
            LOG_INFO("TcpClient connecting to server: {}:{}", host, port);

            // 更新状态为连接中
            connection_state_.store(ConnectionState::Connecting);

            // 创建连接器
            connector_ = sIOContextPool.createConnector();
            if (!connector_)
            {
                LOG_ERROR("Failed to create connector");
                connection_state_.store(ConnectionState::Disconnected);
                onConnectError("Failed to create connector");
                return false;
            }

            // 连接成功回调
            auto connect_cb = [this](tcp::socket&& sock) {
                handleConnectSuccess(std::forward<tcp::socket>(sock));
            };

            // 连接错误回调
            auto error_cb = [this](const std::string& error_msg) {
                handleConnectError(error_msg);
            };

            // 发起连接
            connector_->connect(host, port, connect_cb, error_cb);

            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to connect to server: {}", e.what());
            connection_state_.store(ConnectionState::Disconnected);
            onConnectError(std::string("Exception: ") + e.what());
            return false;
        }
    }

    bool TcpClient::sendMessage(const cncpp::NetworkMessage& message)
    {
        if (!isConnected())
        {
            LOG_ERROR("TcpClient not connected to server");
            return false;
        }

        try
        {
            // todo: 发送消息到会话
            // session_->send(message);
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to send message to server: {}", e.what());
            return false;
        }
    }

    bool TcpClient::sendMessage(const std::string& message)
    {
        if (!isConnected())
        {
            LOG_ERROR("TcpClient not connected to server");
            return false;
        }

        try
        {
            // todo: 发送消息到会话
            // session_->send(message, 0);
            LOG_DEBUG("TcpClient sent message to server: {}", message);
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to send message to server: {}", e.what());
            return false;
        }
    }

    void TcpClient::disconnect()
    {
        ConnectionState current = connection_state_.exchange(ConnectionState::Disconnected);
        if (current == ConnectionState::Disconnected)
        {
            return;
        }

        LOG_INFO("TcpClient disconnecting from server: {}:{}", host_, port_);

        if (session_)
        {
            session_->close();
            session_.reset();
        }

        if (connector_)
        {
            connector_.reset();
        }

        // 调用断开回调
        if (disconnect_callback_)
        {
            disconnect_callback_();
        }

        LOG_INFO("TcpClient disconnected from server");
    }

    bool TcpClient::isConnected() const
    {
        return connection_state_.load() == ConnectionState::Connected && session_ != nullptr && session_->isOpen();
    }

    TcpClient::ConnectionState TcpClient::getConnectionState() const
    {
        return connection_state_.load();
    }

    void TcpClient::setConnectionState(ConnectionState state)
    {
        connection_state_.store(state);
    }

    void TcpClient::handleConnectSuccess(tcp::socket&& sock)
    {
        LOG_INFO("TcpClient connected to server successfully: {}:{}", host_, port_);

        // 创建会话
        session_ = std::make_shared<Session>(std::move(sock));

        // 启动会话
        session_->start();

        // 更新连接状态
        connection_state_.store(ConnectionState::Connected);

        // 调用连接成功钩子
        onConnectSuccess();

        // 调用连接回调
        if (connect_callback_)
        {
            connect_callback_(true, "");
        }
    }

    void TcpClient::handleConnectError(const std::string& error_msg)
    {
        LOG_ERROR("TcpClient failed to connect to server: {}", error_msg);

        // 更新连接状态
        connection_state_.store(ConnectionState::Disconnected);

        // 清理连接器
        connector_.reset();

        // 调用连接失败钩子
        onConnectError(error_msg);

        // 调用连接回调
        if (connect_callback_)
        {
            connect_callback_(false, error_msg);
        }
    }

    void TcpClient::handleMessage(const cncpp::NetworkMessage& message)
    {
        // LOG_DEBUG("TcpClient received message from server:");
        // LOG_DEBUG("  Type: {}", static_cast<int>(message.header_.msg_type_));
        // LOG_DEBUG("  From: {}", message.header_.from_);
        // LOG_DEBUG("  To: {}", message.header_.to_);
        // LOG_DEBUG("  Body: {}", message.body_);

        // 调用消息接收钩子
        onMessageReceived(message);

        // 调用消息回调
        if (msg_callback_)
        {
            try
            {
                msg_callback_(message);
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Error handling message from server: {}", e.what());
            }
        }
    }

    void TcpClient::onConnectSuccess()
    {
        // 默认实现为空，子类可重写
    }

    void TcpClient::onConnectError(const std::string& /*error_msg*/)
    {
        // 默认实现为空，子类可重写
    }

    void TcpClient::onMessageReceived(const NetworkMessage& /*message*/)
    {
        // 默认实现为空，子类可重写
    }

    void TcpClient::onDisconnected()
    {
        LOG_WARN("TcpClient disconnected from server: {}:{}", host_, port_);

        // 更新连接状态
        connection_state_.store(ConnectionState::Disconnected);

        // 清理会话
        session_.reset();

        // 调用断开回调
        if (disconnect_callback_)
        {
            disconnect_callback_();
        }
    }

}  // namespace cncpp
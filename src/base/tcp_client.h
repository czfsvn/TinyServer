#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <string>
#include "network.h"

namespace cncpp
{

    /**
 * @brief TCP客户端组件类
 * 
 * 提供通用的 TCP 客户端功能，支持继承和组合使用。
 * 设计特点：
 * 1. 使用虚函数提供钩子，方便子类扩展
 * 2. 支持回调机制处理异步事件
 * 3. 线程安全的状态管理
 * 4. 可配置的连接参数
 */
    class TcpClient : public std::enable_shared_from_this<TcpClient>
    {
    public:
        using MessageCallback    = std::function<void(const NetworkMessage& message)>;
        using ConnectCallback    = std::function<void(bool success, const std::string& error_msg)>;
        using DisconnectCallback = std::function<void()>;

        /**
     * @brief 连接状态枚举
     */
        enum class ConnectionState
        {
            Disconnected,  // 未连接
            Connecting,    // 连接中
            Connected      // 已连接
        };

        TcpClient();
        virtual ~TcpClient();

        // 禁止拷贝和移动
        TcpClient(const TcpClient&)            = delete;
        TcpClient& operator=(const TcpClient&) = delete;
        TcpClient(TcpClient&&)                 = delete;
        TcpClient& operator=(TcpClient&&)      = delete;

        /**
     * @brief 设置消息回调
     * @param callback 消息接收回调函数
     */
        void setMessageCallback(MessageCallback callback);

        /**
     * @brief 设置连接回调
     * @param callback 连接状态变化回调函数
     */
        void setConnectCallback(ConnectCallback callback);

        /**
     * @brief 设置断开回调
     * @param callback 断开连接回调函数
     */
        void setDisconnectCallback(DisconnectCallback callback);

        /**
     * @brief 连接到服务器
     * @param host 服务器地址
     * @param port 服务器端口
     * @return 是否成功发起连接
     */
        bool connect(const std::string& host, short port);

        /**
     * @brief 发送消息到服务器
     * @param message 网络消息
     * @return 是否发送成功
     */
        bool sendMessage(const NetworkMessage& message);

        /**
     * @brief 发送字符串消息到服务器
     * @param message 字符串消息
     * @return 是否发送成功
     */
        bool sendMessage(const std::string& message);

        /**
     * @brief 断开连接
     */
        void disconnect();

        /**
     * @brief 检查是否已连接
     * @return 是否已连接
     */
        bool isConnected() const;

        /**
     * @brief 获取当前连接状态
     * @return 连接状态
     */
        ConnectionState getConnectionState() const;

        /**
     * @brief 获取服务器地址
     * @return 服务器地址
     */
        std::string getServerHost() const
        {
            return host_;
        }

        /**
     * @brief 获取服务器端口
     * @return 服务器端口
     */
        short getServerPort() const
        {
            return port_;
        }

        /**
     * @brief 获取会话对象
     * @return 会话指针
     */
        std::shared_ptr<Session> getSession() const
        {
            return session_;
        }

    protected:
        /**
     * @brief 处理连接成功（钩子方法，子类可重写）
     */
        virtual void onConnectSuccess();

        /**
     * @brief 处理连接失败（钩子方法，子类可重写）
     * @param error_msg 错误信息
     */
        virtual void onConnectError(const std::string& error_msg);

        /**
     * @brief 处理消息接收（钩子方法，子类可重写）
     * @param message 接收到的消息
     */
        virtual void onMessageReceived(const cncpp::NetworkMessage& message);

        /**
     * @brief 处理连接断开（钩子方法，子类可重写）
     */
        virtual void onDisconnected();

        /**
     * @brief 更新连接状态
     * @param state 新的连接状态
     */
        void setConnectionState(ConnectionState state);

        /**
     * @brief 获取连接器（供子类使用）
     * @return 连接器指针
     */
        std::shared_ptr<Connector> getConnector() const
        {
            return connector_;
        }

    private:
        /**
     * @brief 内部连接成功处理
     */
        void handleConnectSuccess(tcp::socket&& sock);

        /**
     * @brief 内部连接失败处理
     */
        void handleConnectError(const std::string& error_msg);

        /**
     * @brief 内部消息接收处理
     */
        void handleMessage(const cncpp::NetworkMessage& message);

    protected:
        std::string                host_;       // 服务器地址
        short                      port_{0};    // 服务器端口
        std::shared_ptr<Connector> connector_;  // 连接器
        std::shared_ptr<Session>   session_;    // 会话

    private:
        std::atomic<ConnectionState> connection_state_{ConnectionState::Disconnected};
        MessageCallback              msg_callback_;         // 消息回调
        ConnectCallback              connect_callback_;     // 连接回调
        DisconnectCallback           disconnect_callback_;  // 断开回调
    };

    using TcpClientPtr = std::shared_ptr<TcpClient>;

}  // namespace cncpp
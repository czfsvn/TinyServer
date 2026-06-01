#pragma once

#include <atomic>
#include <memory>
#include <string>
#include "tcp_client.h"

/**
 * @brief TinyClient类，用于与TinyServer建立连接并通信
 *
 * 继承自TcpClient，提供与游戏逻辑服务器通信的特定功能。
 * 支持登录验证、消息转发等网关核心功能。
 */
class TinyClient : public cncpp::TcpClient
{
public:
    /**
     * @brief 构造函数
     */
    TinyClient();

    /**
     * @brief 析构函数
     */
    ~TinyClient() override;

    // 禁止拷贝和移动
    TinyClient(const TinyClient&)            = delete;
    TinyClient& operator=(const TinyClient&) = delete;
    TinyClient(TinyClient&&)                 = delete;
    TinyClient& operator=(TinyClient&&)      = delete;

    /**
     * @brief 设置网关ID
     * @param gateway_id 网关唯一标识
     */
    void setGatewayID(uint32_t gateway_id);

    /**
     * @brief 获取网关ID
     * @return 网关唯一标识
     */
    uint32_t getGatewayID() const
    {
        return gateway_id_;
    }

    /**
     * @brief 发送登录验证消息到TinyServer
     * @param user_id 用户ID
     * @param token 认证令牌
     * @return 是否发送成功
     */
    bool sendLoginRequest(uint32_t user_id, const std::string& token);

    /**
     * @brief 发送心跳消息
     * @return 是否发送成功
     */
    bool sendHeartbeat();

    /**
     * @brief 发送用户消息到TinyServer
     * @param message 消息内容
     * @return 是否发送成功
     */
    bool sendUserMessage(const cncpp::NetworkMessage& message);

    /**
     * @brief 获取最后心跳时间
     * @return 最后心跳时间戳
     */
    uint64_t getLastHeartbeatTime() const
    {
        return last_heartbeat_time_;
    }

protected:
    /**
     * @brief 处理连接成功（重写父类钩子）
     */
    void onConnectSuccess() override;

    /**
     * @brief 处理连接失败（重写父类钩子）
     * @param error_msg 错误信息
     */
    void onConnectError(const std::string& error_msg) override;

    /**
     * @brief 处理消息接收（重写父类钩子）
     * @param message 接收到的消息
     */
    void onMessageReceived(const cncpp::NetworkMessage& message) override;

    /**
     * @brief 处理连接断开（重写父类钩子）
     */
    void onDisconnected() override;

private:
    /**
     * @brief 发送网关注册消息到TinyServer
     */
    void sendRegisterMessage();

    /**
     * @brief 处理登录响应
     * @param message 消息内容
     */
    void handleLoginResponse(const cncpp::NetworkMessage& message);

    /**
     * @brief 处理心跳响应
     * @param message 消息内容
     */
    void handleHeartbeatResponse(const cncpp::NetworkMessage& message);

    /**
     * @brief 处理用户消息
     * @param message 消息内容
     */
    void handleUserMessage(const cncpp::NetworkMessage& message);

private:
    uint32_t              gateway_id_;              // 网关唯一标识
    std::atomic<uint64_t> last_heartbeat_time_{0};  // 最后心跳时间戳
    bool                  registered_{false};       // 是否已注册到TinyServer
};

using TinyClientPtr = std::shared_ptr<TinyClient>;

#pragma once

#include <string>
#include "tcp_client.h"

/**
 * @brief 网关客户端类，专用于连接 tinyserver
 * 
 * 继承自 TcpClient，提供网关特定的连接功能。
 * 可通过重写父类钩子方法进行扩展。
 */
class GatewayClient : public cncpp::TcpClient
{
public:
    GatewayClient();
    ~GatewayClient() override;

    // 禁止拷贝和移动
    GatewayClient(const GatewayClient&)            = delete;
    GatewayClient& operator=(const GatewayClient&) = delete;
    GatewayClient(GatewayClient&&)                 = delete;
    GatewayClient& operator=(GatewayClient&&)      = delete;

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

protected:
    /**
     * @brief 处理连接成功（重写父类钩子）
     */
    void onConnectSuccess() override;

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
     * @brief 发送网关注册消息到服务器
     */
    void sendRegisterMessage();

private:
    uint32_t gateway_id_;  // 网关唯一标识
};

using GatewayClientPtr = std::shared_ptr<GatewayClient>;
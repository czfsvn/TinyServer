#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include "gate_user.h"
#include "message.h"
#include "network.h"
#include "tcp_task.h"

// 网关任务类（对应客户端连接）
class GateTask : public cncpp::TcpTask
{
public:
    GateTask(tcp::socket&& socket, const std::string& gateway_id);
    ~GateTask() override;

    // 禁止拷贝和移动
    GateTask(const GateTask&)            = delete;
    GateTask& operator=(const GateTask&) = delete;
    GateTask(GateTask&&)                 = delete;
    GateTask& operator=(GateTask&&)      = delete;

    /**
     * @brief 获取网关ID
     * @return 所属网关ID
     */
    uint32_t getGatewayID() const
    {
        return gateway_id_;
    }

    /**
     * @brief 获取用户
     * @return 关联用户
     */
    GateUserPtr getUser() const
    {
        return user_;
    }

    /**
     * @brief 设置用户
     * @param user 用户指针
     */
    void setUser(GateUserPtr user);

    /**
     * @brief 处理消息
     * @param message 接收到的消息
     */
    void processMessage(const NetworkMessage& message);

protected:
    /**
     * @brief 处理连接成功（重写父类钩子）
     */
    void onConnected() override;

    /**
     * @brief 处理消息（重写父类钩子）
     * @param message 接收到的消息
     */
    void onMessage(const NetworkMessage& message) override;

    /**
     * @brief 处理断开（重写父类钩子）
     */
    void onDisconnected() override;

private:
    /**
     * @brief 处理认证
     * @param message 消息
     * @return 是否认证成功
     */
    bool processAuthentication(const NetworkMessage& message);

    /**
     * @brief 处理业务消息
     * @param message 消息
     */
    void processBusinessMessage(const NetworkMessage& message);

private:
    uint32_t    gateway_id_;  // 所属网关ID
    GateUserPtr user_;        // 关联用户
};

using GateTaskPtr = std::shared_ptr<GateTask>;
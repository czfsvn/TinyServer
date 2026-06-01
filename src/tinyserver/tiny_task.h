#pragma once

#include <cstdint>
#include <memory>
#include "tcp_task.h"

/**
 * @brief TinyServer 任务类（对应客户端连接）
 * 
 * 继承自通用的 TcpTask，提供 TinyServer 特定的功能。
 */
class TinyTask : public cncpp::TcpTask
{
public:
    TinyTask(tcp::socket&& socket);
    ~TinyTask() override;

    // 禁止拷贝和移动
    TinyTask(const TinyTask&)            = delete;
    TinyTask& operator=(const TinyTask&) = delete;
    TinyTask(TinyTask&&)                 = delete;
    TinyTask& operator=(TinyTask&&)      = delete;

    /**
     * @brief 处理消息
     * @param message 接收到的消息
     */
    void processMessage(const cncpp::NetworkMessage& message);

protected:
    /**
     * @brief 处理连接成功（重写父类钩子）
     */
    void onConnected() override;

    /**
     * @brief 处理消息（重写父类钩子）
     * @param message 接收到的消息
     */
    void onMessage(const cncpp::NetworkMessage& message) override;

    /**
     * @brief 处理断开（重写父类钩子）
     */
    void onDisconnected() override;

private:
};

using TinyTaskPtr = std::shared_ptr<TinyTask>;
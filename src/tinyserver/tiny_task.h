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
     * @brief 获取客户端ID
     * @return 客户端唯一标识
     */
    uint32_t getClientID() const
    {
        return client_id_;
    }

    /**
     * @brief 设置客户端ID
     * @param client_id 客户端唯一标识
     */
    void setClientID(uint32_t client_id);

    /**
     * @brief 判断是否已设置客户端ID
     * @return 是否已设置（0 表示未设置）
     */
    bool hasClientID() const
    {
        return client_id_ != 0;
    }

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
    uint32_t client_id_ = 0;  // 客户端唯一标识（0 表示未设置）
};

using TinyTaskPtr = std::shared_ptr<TinyTask>;
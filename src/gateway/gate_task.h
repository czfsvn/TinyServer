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

// 任务状态枚举
enum class TaskStatus
{
    PENDING,       // 待处理
    INITIALIZING,  // 初始化中
    EXECUTING,     // 执行中
    COMPLETED,     // 已完成
    FAILED,        // 失败
    TIMEOUT,       // 超时
    CANCELLED      // 已取消
};

// 任务优先级枚举
enum class TaskPriority
{
    LOW,
    NORMAL,
    HIGH,
    CRITICAL
};

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

    // 获取网关ID
    uint32_t getGatewayID() const
    {
        return gateway_id_;
    }

    // 获取/设置用户
    GateUserPtr getUser() const;
    void        setUser(GateUserPtr user);

    // 任务状态管理
    TaskStatus getStatus() const
    {
        return status_.load();
    }
    void setStatus(TaskStatus status);

    // 任务优先级
    TaskPriority getPriority() const
    {
        return priority_;
    }
    void setPriority(TaskPriority priority)
    {
        priority_ = priority;
    }

    // 生命周期时间戳
    std::chrono::steady_clock::time_point getCreateTime() const
    {
        return create_time_;
    }
    std::chrono::steady_clock::time_point getStartTime() const
    {
        return start_time_;
    }
    std::chrono::steady_clock::time_point getEndTime() const
    {
        return end_time_;
    }

    // 超时控制
    uint32_t getTimeoutMs() const
    {
        return timeout_ms_;
    }
    void setTimeoutMs(uint32_t timeout_ms)
    {
        timeout_ms_ = timeout_ms;
    }
    bool isTimeout() const;

    // 任务生命周期操作
    void startTask();
    void completeTask();
    void failTask(const std::string& error);
    void cancelTask();

    // 处理消息
    void processMessage(const cncpp::NetworkMessage& message);

protected:
    void onConnected() override;
    void onMessage(const cncpp::NetworkMessage& message) override;
    void onDisconnected() override;

private:
    bool processAuthentication(const cncpp::NetworkMessage& message);
    void processBusinessMessage(const cncpp::NetworkMessage& message);

private:
    uint32_t                              gateway_id_;
    GateUserPtr                           user_;
    std::atomic<TaskStatus>               status_;
    TaskPriority                          priority_;
    std::chrono::steady_clock::time_point create_time_;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point end_time_;
    uint32_t                              timeout_ms_;
    std::string                           last_error_;
};

using GateTaskPtr = std::shared_ptr<GateTask>;

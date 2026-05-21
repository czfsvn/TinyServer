#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "gate_task.h"
#include "network.h"
#include "singleton.h"
#include "task_manager.h"

/**
 * @brief 网关任务管理器（单例模式）
 * 
 * 继承自通用的 TaskManager 和 Singleton，提供网关特定的功能。
 */
class GateTaskManager : public cncpp::TaskManager<GateTask>, public cncpp::Singleton<GateTaskManager>
{
public:
    GateTaskManager()  = default;
    ~GateTaskManager() = default;

    // 禁止拷贝和移动
    GateTaskManager(const GateTaskManager&)            = delete;
    GateTaskManager& operator=(const GateTaskManager&) = delete;
    GateTaskManager(GateTaskManager&&)                 = delete;
    GateTaskManager& operator=(GateTaskManager&&)      = delete;

    /**
     * @brief 添加任务（网关特定版本，接受 socket 和 gateway_id）
     * @param socket 客户端socket
     * @param gateway_id 网关ID
     * @return 创建的任务指针
     */
    GateTaskPtr addTask(tcp::socket&& socket, uint32_t gateway_id);

    /**
     * @brief 根据用户ID获取任务
     * @param user_id 用户ID
     * @return 任务指针，如果不存在返回 nullptr
     */
    GateTaskPtr getTaskByUserID(uint32_t user_id) const;

    /**
     * @brief 获取指定状态的任务列表
     * @param state 任务状态
     * @return 任务列表
     */
    std::vector<GateTaskPtr> getTasksByState(TaskState state) const;

    /**
     * @brief 向指定用户发送消息
     * @param user_id 用户ID
     * @param message 消息
     * @return 是否发送成功
     */
    bool sendMessageToUser(uint32_t user_id, const NetworkMessage& message);

private:
    mutable std::mutex                        user_mutex_;    // 用户映射保护锁
    std::unordered_map<uint32_t, GateTaskPtr> user_to_task_;  // user_id -> GateTask
};

#define sGateTaskManager GateTaskManager::getMe()
using GateTaskManagerPtr = std::shared_ptr<GateTaskManager>;
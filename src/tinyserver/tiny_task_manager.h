#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "network.h"
#include "singleton.h"
#include "task_manager.h"
#include "tiny_task.h"

/**
 * @brief TinyServer 任务管理器（单例模式）
 * 
 * 继承自通用的 TaskManager 和 Singleton，提供 TinyServer 特定的功能。
 */
class TinyTaskManager : public cncpp::TaskManager<TinyTask>, public cncpp::Singleton<TinyTaskManager>
{
public:
    TinyTaskManager()  = default;
    ~TinyTaskManager() = default;

    // 禁止拷贝和移动
    TinyTaskManager(const TinyTaskManager&)            = delete;
    TinyTaskManager& operator=(const TinyTaskManager&) = delete;
    TinyTaskManager(TinyTaskManager&&)                 = delete;
    TinyTaskManager& operator=(TinyTaskManager&&)      = delete;

    /**
     * @brief 添加任务（TinyServer 特定版本，接受 socket）
     * @param socket 客户端socket
     * @return 创建的任务指针
     */
    TinyTaskPtr addTask(tcp::socket&& socket);

    /**
     * @brief 移除任务（重写父类方法，添加客户端映射清理）
     * @param task_id 任务ID
     */
    void removeTask(uint32_t task_id);

    /**
     * @brief 根据客户端ID获取任务
     * @param client_id 客户端ID
     * @return 任务指针，如果不存在返回 nullptr
     */
    TinyTaskPtr getTaskByClientID(uint32_t client_id) const;

    /**
     * @brief 更新客户端ID到任务的映射
     * @param client_id 客户端ID
     * @param task 任务指针
     */
    void updateClientMapping(uint32_t client_id, TinyTaskPtr task);

    /**
     * @brief 移除客户端ID映射
     * @param client_id 客户端ID
     */
    void removeClientMapping(uint32_t client_id);

    /**
     * @brief 获取指定IP的任务列表
     * @param ip_address IP地址
     * @return 任务列表
     */
    std::vector<TinyTaskPtr> getTasksByIP(const std::string& ip_address) const;

    /**
     * @brief 向指定客户端发送消息
     * @param client_id 客户端ID
     * @param message 消息
     * @return 是否发送成功
     */
    bool sendMessageToClient(uint32_t client_id, const cncpp::NetworkMessage& message);

private:
    mutable std::mutex                        client_mutex_;       // 客户端映射保护锁
    std::unordered_map<uint32_t, TinyTaskPtr> client_id_to_task_;  // client_id -> TinyTask
};

#define sTinyTaskManager TinyTaskManager::getMe()
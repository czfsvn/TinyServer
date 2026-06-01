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
     * @brief 移除任务（重写父类方法）
     * @param task_id 任务ID
     */
    void removeTask(uint32_t task_id);

    /**
     * @brief 获取指定IP的任务列表
     * @param ip_address IP地址
     * @return 任务列表
     */
    std::vector<TinyTaskPtr> getTasksByIP(const std::string& ip_address) const;

private:
    // 预留：可在此添加 TinyServer 特定的成员变量
};

#define sTinyTaskManager TinyTaskManager::getMe()
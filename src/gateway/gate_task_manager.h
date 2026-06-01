#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include "gate_task.h"
#include "singleton.h"
#include "task_manager.h"

class GateTaskManager : public cncpp::TaskManager<GateTask>, public cncpp::Singleton<GateTaskManager>
{
public:
    struct TaskStats
    {
        uint64_t total_tasks     = 0;
        uint64_t completed_tasks = 0;
        uint64_t failed_tasks    = 0;
        uint64_t timeout_tasks   = 0;
        uint64_t cancelled_tasks = 0;
        size_t   active_tasks    = 0;
    };

    GateTaskManager();
    ~GateTaskManager();

    GateTaskManager(const GateTaskManager&)            = delete;
    GateTaskManager& operator=(const GateTaskManager&) = delete;
    GateTaskManager(GateTaskManager&&)                 = delete;
    GateTaskManager& operator=(GateTaskManager&&)      = delete;

    // 添加任务
    GateTaskPtr addTask(tcp::socket&& socket, const std::string& gateway_id);

    // 移除任务
    void removeTask(uint32_t task_id);

    // 获取任务
    GateTaskPtr getTask(uint32_t task_id);
    GateTaskPtr getTaskByUserID(uint32_t user_id) const;

    // 获取任务列表
    std::vector<GateTaskPtr> getTasksByStatus(TaskStatus status);
    std::vector<GateTaskPtr> getAllTasks();

    // 消息发送
    bool sendMessageToUser(uint32_t user_id, const cncpp::NetworkMessage& message);

    // 任务调度器
    void startTaskScheduler();
    void stopTaskScheduler();

    // 任务调度
    void scheduleTask(GateTaskPtr task);
    void processTaskQueue();

    // 超时清理
    void cleanupTimeoutTasks();

    // 统计信息
    TaskStats getStats() const;
    void      resetStats();

private:
    // 任务优先级比较器
    struct TaskCompare
    {
        bool operator()(const GateTaskPtr& a, const GateTaskPtr& b) const
        {
            return static_cast<int>(a->getPriority()) < static_cast<int>(b->getPriority());
        }
    };

    // 用户映射
    mutable std::mutex                        user_mutex_;
    std::unordered_map<uint32_t, GateTaskPtr> user_to_task_;

    // 任务队列
    mutable std::mutex                                                      queue_mutex_;
    std::priority_queue<GateTaskPtr, std::vector<GateTaskPtr>, TaskCompare> task_queue_;
    std::condition_variable                                                 queue_cv_;

    // 调度器线程
    std::atomic<bool> scheduler_running_{false};
    std::thread       scheduler_thread_;

    // 统计信息
    mutable std::mutex stats_mutex_;
    TaskStats          stats_;
};

#define sGateTaskManager GateTaskManager::getMe()
using GateTaskManagerPtr = std::shared_ptr<GateTaskManager>;

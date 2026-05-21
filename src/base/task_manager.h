#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cncpp
{

    /**
 * @brief 通用任务管理器模板类
 * 
 * 提供通用的任务管理功能，支持任意类型的任务。
 * 
 * 设计特点：
 *   1. 模板化设计，支持任意任务类型
 *   2. 线程安全的任务管理
 *   3. 支持任务状态管理、超时清理、广播等功能
 * 
 * @tparam TaskType 任务类型，需要满足：
 *   - 继承自 std::enable_shared_from_this
 *   - 具有 getTaskID() 方法返回 uint32_t
 *   - 具有 isActive() 方法返回 bool
 *   - 具有 getLastActiveTime() 方法返回时间点
 *   - 具有 stop() 方法
 *   - 具有 sendMessage() 方法
 */
    template <typename TaskType>
    class TaskManager
    {
    public:
        using TaskPtr = std::shared_ptr<TaskType>;

        TaskManager()          = default;
        virtual ~TaskManager() = default;

        // 禁止拷贝和移动
        TaskManager(const TaskManager&)            = delete;
        TaskManager& operator=(const TaskManager&) = delete;
        TaskManager(TaskManager&&)                 = delete;
        TaskManager& operator=(TaskManager&&)      = delete;

        /**
     * @brief 添加任务
     * @param task 任务指针
     */
        void addTask(TaskPtr task)
        {
            if (!task)
                return;

            std::lock_guard<std::mutex> lock(mutex_);
            tasks_[task->getTaskID()] = task;
        }

        /**
     * @brief 获取任务
     * @param task_id 任务ID
     * @return 任务指针，如果不存在返回 nullptr
     */
        TaskPtr getTask(uint32_t task_id) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto                        it = tasks_.find(task_id);
            if (it != tasks_.end())
                return it->second;
            return nullptr;
        }

        /**
     * @brief 移除任务
     * @param task_id 任务ID
     */
        void removeTask(uint32_t task_id)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.erase(task_id);
        }

        /**
     * @brief 判断任务是否存在
     * @param task_id 任务ID
     * @return 是否存在
     */
        bool hasTask(uint32_t task_id) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return tasks_.find(task_id) != tasks_.end();
        }

        /**
     * @brief 获取任务数量
     * @return 任务总数
     */
        size_t getTaskCount() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return tasks_.size();
        }

        /**
     * @brief 获取所有任务ID
     * @return 任务ID列表
     */
        std::vector<uint32_t> getAllTaskIDs() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<uint32_t>       ids;
            ids.reserve(tasks_.size());
            for (const auto& pair : tasks_)
            {
                ids.push_back(pair.first);
            }
            return ids;
        }

        /**
     * @brief 获取所有任务
     * @return 任务列表
     */
        std::vector<TaskPtr> getAllTasks() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<TaskPtr>        task_list;
            task_list.reserve(tasks_.size());
            for (const auto& pair : tasks_)
            {
                task_list.push_back(pair.second);
            }
            return task_list;
        }

        /**
     * @brief 获取活跃任务数量
     * @return 活跃任务数量
     */
        size_t getActiveTaskCount() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            size_t                      count = 0;
            for (const auto& pair : tasks_)
            {
                if (pair.second->isActive())
                    ++count;
            }
            return count;
        }

        /**
     * @brief 停止所有任务
     */
        void stopAllTasks()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& pair : tasks_)
            {
                pair.second->stop();
            }
            tasks_.clear();
        }

        /**
     * @brief 清理超时任务
     * @param timeout 超时时间（默认5分钟）
     * @return 清理的任务数量
     */
        size_t cleanupTimeoutTasks(std::chrono::seconds timeout = std::chrono::seconds(300))
        {
            std::lock_guard<std::mutex> lock(mutex_);
            size_t                      cleaned = 0;
            auto                        now     = std::chrono::steady_clock::now();

            for (auto it = tasks_.begin(); it != tasks_.end();)
            {
                auto& task        = it->second;
                auto  last_active = task->getLastActiveTime();
                auto  elapsed     = std::chrono::duration_cast<std::chrono::seconds>(now - last_active);

                if (elapsed >= timeout)
                {
                    task->stop();
                    it = tasks_.erase(it);
                    ++cleaned;
                }
                else
                {
                    ++it;
                }
            }

            return cleaned;
        }

        /**
     * @brief 向所有活跃任务广播消息
     * @param message 消息
     * @return 成功发送的任务数量
     */
        template <typename MessageType>
        size_t broadcastMessage(const MessageType& message)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            size_t                      sent = 0;

            for (const auto& pair : tasks_)
            {
                if (pair.second->isActive())
                {
                    if (pair.second->sendMessage(message))
                        ++sent;
                }
            }

            return sent;
        }

        /**
     * @brief 遍历所有任务
     * @param callback 回调函数，参数为任务指针，返回 bool 表示是否继续遍历
     * @return 遍历的任务数量
     */
        size_t foreach (std::function<bool(TaskPtr)> callback) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            size_t                      count = 0;

            for (const auto& pair : tasks_)
            {
                ++count;
                if (!callback(pair.second))
                    break;
            }

            return count;
        }

        /**
     * @brief 遍历所有任务（无返回值版本）
     * @param callback 回调函数，参数为任务指针
     */
        void foreach (std::function<void(TaskPtr)> callback) const
        {
            std::lock_guard<std::mutex> lock(mutex_);

            for (const auto& pair : tasks_)
            {
                callback(pair.second);
            }
        }

        /**
     * @brief 遍历活跃任务
     * @param callback 回调函数，参数为任务指针，返回 bool 表示是否继续遍历
     * @return 遍历的活跃任务数量
     */
        size_t foreachActive(std::function<bool(TaskPtr)> callback) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            size_t                      count = 0;

            for (const auto& pair : tasks_)
            {
                if (pair.second->isActive())
                {
                    ++count;
                    if (!callback(pair.second))
                        break;
                }
            }

            return count;
        }

        /**
     * @brief 遍历活跃任务（无返回值版本）
     * @param callback 回调函数，参数为任务指针
     */
        void foreachActive(std::function<void(TaskPtr)> callback) const
        {
            std::lock_guard<std::mutex> lock(mutex_);

            for (const auto& pair : tasks_)
            {
                if (pair.second->isActive())
                {
                    callback(pair.second);
                }
            }
        }

    protected:
        mutable std::mutex                    mutex_;  // 保护锁
        std::unordered_map<uint32_t, TaskPtr> tasks_;  // task_id -> Task
    };

}  // namespace cncpp
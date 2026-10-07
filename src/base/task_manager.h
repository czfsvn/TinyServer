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

    // drainInboundMessages() 的默认模板实参。这里只做前置声明、不 include
    // message.h，是为了让这个通用任务管理器不依赖网络层。
    struct NetworkMessage;

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

        /**
     * @brief 每拍把所有任务的接收队列抽干（ADR-0004）
     *
     * 三个服务端原本各有一份逐字重复的实现，收口到这里：消息一律交回
     * TaskType::processMessage()，由任务自己分发。
     *
     * 注意它不能用 foreach()/foreachActive() —— 那两个是持锁回调的，而分发绝不能
     * 握着这把锁：io 线程 accept 到新连接要在 addTask 上拿同一把锁，一次慢分发
     * 会把它挡在门外。所以这里先拷快照、解锁之后再分发。
     *
     * @param max_per_task 每个任务本拍最多处理多少条。0 = 不限（抽干）。
     * @param max_total    本拍所有任务合计最多处理多少条。0 = 不限。它是硬上界：
     *                     max_per_task 挡不住连接数这个维度（1 万连接 × 每连接
     *                     上限 64 条 = 64 万条/拍，照样跑爆一拍）。
     * @return 本拍分发的消息总数
     */
        template <typename MessageType = NetworkMessage>
        size_t drainInboundMessages(uint32_t max_per_task = 0, uint32_t max_total = 0)
        {
            const std::vector<TaskPtr> tasks = getAllTasks();
            if (tasks.empty())
            {
                return 0;
            }

            size_t message_count = 0;

            // 第一遍：已停止的任务全量抽干，不受任何预算约束（A6）。
            // close() 之后队列里已解码的帧仍然要交给业务，留到下一拍 session_ 就已
            // 置空，这些帧永远没人取了。已停止的任务不会再有新消息进来，残留量有界，
            // 所以这里不会变成新的耗时漏洞。
            for (const auto& task : tasks)
            {
                if (!task->isActive())
                {
                    message_count += drainOneTask<MessageType>(task, 0);
                }
            }

            // 第二遍：活跃任务，轮转 + 整拍预算。
            // 轮转是预算的配套：有了上限之后每拍只能服务前若干个任务，如果每拍都
            // 从头开始，靠后的任务会被永久饿死。
            const size_t start = drain_cursor_ % tasks.size();
            size_t       i     = 0;
            for (; i < tasks.size(); ++i)
            {
                if (max_total > 0 && message_count >= max_total)
                {
                    break;
                }

                const auto& task = tasks[(start + i) % tasks.size()];

                // 已停止的任务不限量（A6）。第一遍抽的是快照时刻就已经停了的，这里
                // 兜住快照之后才被 io 线程 stop 掉的：它们不再有新消息进来，若受预算
                // 约束留下残留，下一拍 session_ 已置空，这些帧就永远没人取了。
                // 对第一遍已抽空的 task，再 pop 一次直接返回 false，成本是一次原子读。
                if (!task->isActive())
                {
                    message_count += drainOneTask<MessageType>(task, 0);
                    continue;
                }

                // 本拍额度受两个上限约束，取更紧的那个
                uint32_t budget = max_per_task;
                if (max_total > 0)
                {
                    const uint32_t remaining = static_cast<uint32_t>(max_total - message_count);
                    budget = (max_per_task == 0 || max_per_task > remaining) ? remaining : max_per_task;
                }
                message_count += drainOneTask<MessageType>(task, budget);
            }

            // 下次从断点继续。跑完一圈时 (start + size) % size == start，自然回到原点
            drain_cursor_ = (start + i) % tasks.size();

            return message_count;
        }

    protected:
        /// 抽干单个任务，最多 max_count 条（0 = 不限）。返回实际处理条数。
        template <typename MessageType>
        size_t drainOneTask(const TaskPtr& task, uint32_t max_count)
        {
            // 任务 stop() 之后 session_ 会被置空，而这是一拍开头拿的快照。
            const auto session = task->getSession();
            if (!session)
            {
                return 0;
            }

            MessageType message;
            uint32_t    handled = 0;
            size_t      count   = 0;
            while (session->getReceiveQueue().tryPop(message))
            {
                task->processMessage(message);
                ++count;

                if (max_count > 0 && ++handled >= max_count)
                {
                    break;
                }
            }
            return count;
        }

        mutable std::mutex                    mutex_;  // 保护锁
        std::unordered_map<uint32_t, TaskPtr> tasks_;  // task_id -> Task
        // 轮转游标：下一拍从哪个任务开始。只在 tick 线程上读写，无需加锁。
        // 注意 tasks_ 是 unordered_map，插入/删除后遍历顺序会重排，所以轮转只能
        // 消除"每拍都从头开始"的系统性偏差，做不到严格公平。
        size_t drain_cursor_ = 0;
    };

}  // namespace cncpp
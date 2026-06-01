#include "gate_task_manager.h"
#include "logger.h"

GateTaskManager::GateTaskManager()
{
}

GateTaskManager::~GateTaskManager()
{
    stopTaskScheduler();
}

GateTaskPtr GateTaskManager::addTask(tcp::socket&& socket, const std::string& gateway_id)
{
    auto task = std::make_shared<GateTask>(std::move(socket), gateway_id);
    if (!task)
    {
        LOG_ERROR("Failed to create GateTask");
        return nullptr;
    }

    TaskManager<GateTask>::addTask(task);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_tasks++;
    }

    scheduleTask(task);

    LOG_INFO("Added GateTask: {}", task->getTaskID());
    return task;
}

void GateTaskManager::removeTask(uint32_t task_id)
{
    auto task = getTask(task_id);
    if (task)
    {
        auto user = task->getUser();
        if (user)
        {
            std::lock_guard<std::mutex> lock(user_mutex_);
            user_to_task_.erase(user->getUserID());
        }
    }

    TaskManager<GateTask>::removeTask(task_id);
}

GateTaskPtr GateTaskManager::getTask(uint32_t task_id)
{
    return TaskManager<GateTask>::getTask(task_id);
}

GateTaskPtr GateTaskManager::getTaskByUserID(uint32_t user_id) const
{
    std::lock_guard<std::mutex> lock(user_mutex_);

    auto it = user_to_task_.find(user_id);
    if (it != user_to_task_.end())
    {
        return it->second;
    }
    return nullptr;
}

std::vector<GateTaskPtr> GateTaskManager::getTasksByStatus(TaskStatus status)
{
    auto all_tasks = getAllTasks();

    std::vector<GateTaskPtr> result;
    for (const auto& task : all_tasks)
    {
        if (task->getStatus() == status)
        {
            result.push_back(task);
        }
    }
    return result;
}

std::vector<GateTaskPtr> GateTaskManager::getAllTasks()
{
    return TaskManager<GateTask>::getAllTasks();
}

bool GateTaskManager::sendMessageToUser(uint32_t user_id, const cncpp::NetworkMessage& message)
{
    std::lock_guard<std::mutex> lock(user_mutex_);

    auto it = user_to_task_.find(user_id);
    if (it != user_to_task_.end())
    {
        return it->second->sendMessage(message);
    }

    LOG_WARN("User {} not found or not connected", user_id);
    return false;
}

void GateTaskManager::startTaskScheduler()
{
    if (scheduler_running_.exchange(true))
    {
        return;
    }

    scheduler_thread_ = std::thread([this]() {
        while (scheduler_running_)
        {
            processTaskQueue();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    LOG_INFO("Task scheduler started");
}

void GateTaskManager::stopTaskScheduler()
{
    if (!scheduler_running_.exchange(false))
    {
        return;
    }

    queue_cv_.notify_all();

    if (scheduler_thread_.joinable())
    {
        scheduler_thread_.join();
    }

    LOG_INFO("Task scheduler stopped");
}

void GateTaskManager::scheduleTask(GateTaskPtr task)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    task_queue_.push(task);
    queue_cv_.notify_one();
}

void GateTaskManager::processTaskQueue()
{
    std::unique_lock<std::mutex> lock(queue_mutex_);

    while (!task_queue_.empty())
    {
        auto task = task_queue_.top();
        task_queue_.pop();

        lock.unlock();

        if (task->getStatus() == TaskStatus::PENDING)
        {
            task->startTask();
        }

        lock.lock();
    }
}

void GateTaskManager::cleanupTimeoutTasks()
{
    auto all_tasks = getAllTasks();

    for (const auto& task : all_tasks)
    {
        if (task->isTimeout())
        {
            LOG_WARN("Task {} timeout, cleaning up", task->getTaskID());
            task->failTask("Task timeout");

            {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                stats_.timeout_tasks++;
            }

            removeTask(task->getTaskID());
        }
    }
}

GateTaskManager::TaskStats GateTaskManager::getStats() const
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void GateTaskManager::resetStats()
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = TaskStats();
}

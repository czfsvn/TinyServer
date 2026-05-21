#include "gate_task_manager.h"
#include "logger.h"

GateTaskPtr GateTaskManager::addTask(tcp::socket&& socket, const std::string& gateway_id)
{
    auto task = std::make_shared<GateTask>(std::move(socket), gateway_id);

    // 使用基类的 addTask 方法
    TaskManager<GateTask>::addTask(task);

    LOG_INFO("Added task: {}", task->getTaskID());
    return task;
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

std::vector<GateTaskPtr> GateTaskManager::getTasksByState(TaskState state) const
{
    auto all_tasks = getAllTasks();

    std::vector<GateTaskPtr> result;
    for (const auto& task : all_tasks)
    {
        if (task->getState() == state)
        {
            result.push_back(task);
        }
    }
    return result;
}

bool GateTaskManager::sendMessageToUser(uint32_t user_id, const NetworkMessage& message)
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
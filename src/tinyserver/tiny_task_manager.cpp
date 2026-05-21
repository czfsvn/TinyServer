#include "tiny_task_manager.h"
#include "logger.h"

TinyTaskPtr TinyTaskManager::addTask(tcp::socket&& socket)
{
    auto task = std::make_shared<TinyTask>(std::move(socket));

    // 使用基类的 addTask 方法
    TaskManager<TinyTask>::addTask(task);

    LOG_INFO("Added TinyTask: {}", task->getTaskID());
    return task;
}

void TinyTaskManager::removeTask(uint32_t task_id)
{
    // 先获取任务
    auto task = getTask(task_id);

    // 如果任务有 client_id，从映射中移除
    if (task && task->hasClientID())
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_id_to_task_.erase(task->getClientID());
        LOG_DEBUG("Removed client mapping for task {}: {}", task_id, task->getClientID());
    }

    // 调用父类方法
    TaskManager<TinyTask>::removeTask(task_id);
}

TinyTaskPtr TinyTaskManager::getTaskByClientID(uint32_t client_id) const
{
    std::lock_guard<std::mutex> lock(client_mutex_);

    auto it = client_id_to_task_.find(client_id);
    if (it != client_id_to_task_.end())
    {
        return it->second;
    }
    return nullptr;
}

void TinyTaskManager::updateClientMapping(uint32_t client_id, TinyTaskPtr task)
{
    std::lock_guard<std::mutex> lock(client_mutex_);
    client_id_to_task_[client_id] = task;
    LOG_DEBUG("Updated client mapping: {} -> task {}", client_id, task->getTaskID());
}

void TinyTaskManager::removeClientMapping(uint32_t client_id)
{
    std::lock_guard<std::mutex> lock(client_mutex_);
    client_id_to_task_.erase(client_id);
    LOG_DEBUG("Removed client mapping: {}", client_id);
}

std::vector<TinyTaskPtr> TinyTaskManager::getTasksByIP(const std::string& ip_address) const
{
    auto all_tasks = getAllTasks();

    std::vector<TinyTaskPtr> result;
    for (const auto& task : all_tasks)
    {
        if (task->getClientIP() == ip_address)
        {
            result.push_back(task);
        }
    }
    return result;
}

bool TinyTaskManager::sendMessageToClient(uint32_t client_id, const cncpp::NetworkMessage& message)
{
    std::lock_guard<std::mutex> lock(client_mutex_);

    auto it = client_id_to_task_.find(client_id);
    if (it != client_id_to_task_.end())
    {
        return it->second->sendMessage(message);
    }

    LOG_WARN("Client {} not found or not connected", client_id);
    return false;
}
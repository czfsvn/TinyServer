#include "data_task_manager.h"
#include "logger.h"

DataTaskPtr DataTaskManager::addTask(tcp::socket&& socket)
{
    auto task = std::make_shared<DataTask>(std::move(socket));
    if (!task)
    {
        LOG_ERROR("Failed to create DataTask");
        return nullptr;
    }

    TaskManager<DataTask>::addTask(task);
    LOG_INFO("Added DataTask: {}", task->getTaskID());
    return task;
}

void DataTaskManager::removeTask(uint32_t task_id)
{
    TaskManager<DataTask>::removeTask(task_id);
}

std::vector<DataTaskPtr> DataTaskManager::getTasksByIP(const std::string& ip_address) const
{
    auto all_tasks = getAllTasks();
    std::vector<DataTaskPtr> result;
    for (const auto& task : all_tasks)
    {
        if (task->getClientIP() == ip_address)
        {
            result.push_back(task);
        }
    }
    return result;
}

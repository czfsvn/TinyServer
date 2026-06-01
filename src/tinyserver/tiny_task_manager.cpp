#include "tiny_task_manager.h"
#include "logger.h"

TinyTaskPtr TinyTaskManager::addTask(tcp::socket&& socket)
{
    auto task = std::make_shared<TinyTask>(std::move(socket));
    if (!task)
    {
        LOG_ERROR("Failed to create TinyTask");
        return nullptr;
    }

    TaskManager<TinyTask>::addTask(task);
    LOG_INFO("Added TinyTask: {}", task->getTaskID());
    return task;
}

void TinyTaskManager::removeTask(uint32_t task_id)
{
    // 调用父类方法
    TaskManager<TinyTask>::removeTask(task_id);
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
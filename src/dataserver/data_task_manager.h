#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "singleton.h"
#include "task_manager.h"
#include "data_task.h"

class DataTaskManager : public cncpp::TaskManager<DataTask>, public cncpp::Singleton<DataTaskManager>
{
public:
    DataTaskManager()  = default;
    ~DataTaskManager() = default;

    DataTaskManager(const DataTaskManager&)            = delete;
    DataTaskManager& operator=(const DataTaskManager&) = delete;
    DataTaskManager(DataTaskManager&&)                 = delete;
    DataTaskManager& operator=(DataTaskManager&&)      = delete;

    DataTaskPtr addTask(tcp::socket&& socket);
    void removeTask(uint32_t task_id);
    std::vector<DataTaskPtr> getTasksByIP(const std::string& ip_address) const;
};

#define sDataTaskManager DataTaskManager::getMe()

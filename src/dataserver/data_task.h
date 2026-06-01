#pragma once

#include <cstdint>
#include <memory>
#include "tcp_task.h"

class DataTask : public cncpp::TcpTask
{
public:
    DataTask(tcp::socket&& socket);
    ~DataTask() override;

    DataTask(const DataTask&)            = delete;
    DataTask& operator=(const DataTask&) = delete;
    DataTask(DataTask&&)                 = delete;
    DataTask& operator=(DataTask&&)      = delete;

    void processMessage(const cncpp::NetworkMessage& message);

protected:
    void onConnected() override;
    void onMessage(const cncpp::NetworkMessage& message) override;
    void onDisconnected() override;

private:
    void handleDatabaseRequest(const cncpp::NetworkMessage& message);
    void sendResponse(uint32_t request_id, uint32_t error_code, const std::string& data);
};

using DataTaskPtr = std::shared_ptr<DataTask>;

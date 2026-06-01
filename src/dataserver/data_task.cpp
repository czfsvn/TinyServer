#include "data_task.h"
#include "logger.h"
#include "data_processor.h"

DataTask::DataTask(tcp::socket&& socket) : cncpp::TcpTask(std::move(socket))
{
    LOG_DEBUG("DataTask created, task_id: {}", getTaskID());
}

DataTask::~DataTask()
{
}

void DataTask::processMessage(const cncpp::NetworkMessage& message)
{
    LOG_DEBUG("DataTask {} received message: {}", getTaskID(), message.body_);
    handleDatabaseRequest(message);
}

void DataTask::onConnected()
{
    cncpp::TcpTask::onConnected();
    LOG_INFO("DataTask client connected: {}", getClientIP());
}

void DataTask::onMessage(const cncpp::NetworkMessage& message)
{
    cncpp::TcpTask::onMessage(message);
    processMessage(message);
}

void DataTask::onDisconnected()
{
    LOG_INFO("DataTask client disconnected: {}", getClientIP());
    cncpp::TcpTask::onDisconnected();
}

void DataTask::handleDatabaseRequest(const cncpp::NetworkMessage& message)
{
    try
    {
        uint32_t request_type = message.header_.message_id_;
        std::string response = sDataProcessor.processRequest(request_type, message.body_);
        sendResponse(message.header_.message_id_, 0, response);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("DataTask {} error processing request: {}", getTaskID(), e.what());
        sendResponse(message.header_.message_id_, 1, std::string("Error: ") + e.what());
    }
}

void DataTask::sendResponse(uint32_t request_id, uint32_t error_code, const std::string& data)
{
    (void)request_id;
    (void)error_code;
    (void)data;
    LOG_DEBUG("DataTask {} sending response, request_id: {}, error_code: {}", 
              getTaskID(), request_id, error_code);
}

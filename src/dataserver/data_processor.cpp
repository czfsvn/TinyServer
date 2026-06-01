#include "data_processor.h"
#include "logger.h"
#include "message_ids.h"

namespace cncpp
{

    DataProcessor::DataProcessor()
    {
        registerHandlers();
    }

    bool DataProcessor::init()
    {
        LOG_INFO("DataProcessor initialized with {} handlers", handlers_.size());
        return true;
    }

    void DataProcessor::registerHandlers()
    {
        using namespace cncpp;
        handlers_[toUint32(MessageId::MSG_ID_USER_LOGIN)]
            = std::bind(&DataProcessor::handleUserLogin, this, std::placeholders::_1, std::placeholders::_2);
        handlers_[toUint32(MessageId::MSG_ID_USER_REGISTER)]
            = std::bind(&DataProcessor::handleUserRegister, this, std::placeholders::_1, std::placeholders::_2);
        handlers_[toUint32(MessageId::MSG_ID_USER_QUERY)]
            = std::bind(&DataProcessor::handleUserQuery, this, std::placeholders::_1, std::placeholders::_2);
        handlers_[toUint32(MessageId::MSG_ID_USER_UPDATE)]
            = std::bind(&DataProcessor::handleUserUpdate, this, std::placeholders::_1, std::placeholders::_2);
        handlers_[toUint32(MessageId::MSG_ID_USER_DELETE)]
            = std::bind(&DataProcessor::handleUserDelete, this, std::placeholders::_1, std::placeholders::_2);
        handlers_[toUint32(MessageId::MSG_ID_EXECUTE_SQL)]
            = std::bind(&DataProcessor::handleExecuteSQL, this, std::placeholders::_1, std::placeholders::_2);
    }

    std::string DataProcessor::processRequest(uint32_t request_type, const std::string& request_data)
    {
        auto it = handlers_.find(request_type);
        if (it == handlers_.end())
        {
            LOG_WARN("Unknown request type: {}", request_type);
            return "{\"error\": \"Unknown request type\"}";
        }

        auto conn = sMySQLPool.acquire();
        if (!conn)
        {
            LOG_ERROR("Failed to acquire MySQL connection");
            return "{\"error\": \"Database connection failed\"}";
        }

        try
        {
            return it->second(request_data, conn);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Request processing failed: {}", e.what());
            return "{\"error\": \"" + std::string(e.what()) + "\"}";
        }
    }

    std::string DataProcessor::handleUserLogin(const std::string& data, std::shared_ptr<MYSQL> conn)
    {
        (void)data;
        (void)conn;
        LOG_DEBUG("Handling user login request");
        return "{\"result\": \"success\", \"user_id\": 1}";
    }

    std::string DataProcessor::handleUserRegister(const std::string& data, std::shared_ptr<MYSQL> conn)
    {
        (void)data;
        (void)conn;
        LOG_DEBUG("Handling user register request");
        return "{\"result\": \"success\", \"user_id\": 2}";
    }

    std::string DataProcessor::handleUserQuery(const std::string& data, std::shared_ptr<MYSQL> conn)
    {
        (void)data;
        (void)conn;
        LOG_DEBUG("Handling user query request");
        return "{\"result\": \"success\", \"data\": []}";
    }

    std::string DataProcessor::handleUserUpdate(const std::string& data, std::shared_ptr<MYSQL> conn)
    {
        (void)data;
        (void)conn;
        LOG_DEBUG("Handling user update request");
        return "{\"result\": \"success\"}";
    }

    std::string DataProcessor::handleUserDelete(const std::string& data, std::shared_ptr<MYSQL> conn)
    {
        (void)data;
        (void)conn;
        LOG_DEBUG("Handling user delete request");
        return "{\"result\": \"success\"}";
    }

    std::string DataProcessor::handleExecuteSQL(const std::string& data, std::shared_ptr<MYSQL> conn)
    {
        (void)data;
        (void)conn;
        LOG_DEBUG("Handling execute SQL request");
        return "{\"result\": \"success\", \"affected_rows\": 0}";
    }

}  // namespace cncpp

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "mysql_conn.h"
#include "singleton.h"

class DataProcessor : public cncpp::Singleton<DataProcessor>
{
public:
    using RequestHandler = std::function<std::string(const std::string&)>;

    DataProcessor();
    ~DataProcessor() = default;

    bool        init();
    std::string processRequest(uint32_t request_type, const std::string& request_data);

private:
    void registerHandlers();

    std::string handleUserLogin(const std::string& data);
    std::string handleUserRegister(const std::string& data);
    std::string handleUserQuery(const std::string& data);
    std::string handleUserUpdate(const std::string& data);
    std::string handleUserDelete(const std::string& data);
    std::string handleExecuteSQL(const std::string& data);

    std::unordered_map<uint32_t, RequestHandler> handlers_;
};

#define sDataProcessor DataProcessor::getMe()

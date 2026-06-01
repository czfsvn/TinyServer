#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "mysql_connection_pool.h"

namespace cncpp
{

    class DataProcessor
    {
    public:
        using RequestHandler = std::function<std::string(const std::string&, std::shared_ptr<MYSQL>)>;

        DataProcessor();
        ~DataProcessor() = default;

        bool        init();
        std::string processRequest(uint32_t request_type, const std::string& request_data);

    private:
        void        registerHandlers();
        std::string handleUserLogin(const std::string& data, std::shared_ptr<MYSQL> conn);
        std::string handleUserRegister(const std::string& data, std::shared_ptr<MYSQL> conn);
        std::string handleUserQuery(const std::string& data, std::shared_ptr<MYSQL> conn);
        std::string handleUserUpdate(const std::string& data, std::shared_ptr<MYSQL> conn);
        std::string handleUserDelete(const std::string& data, std::shared_ptr<MYSQL> conn);
        std::string handleExecuteSQL(const std::string& data, std::shared_ptr<MYSQL> conn);

        std::unordered_map<uint32_t, RequestHandler> handlers_;
    };

}  // namespace cncpp

#define sDataProcessor cncpp::Singleton<cncpp::DataProcessor>::getMe()

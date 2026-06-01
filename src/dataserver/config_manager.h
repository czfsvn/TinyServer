#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include "singleton.h"

namespace cncpp
{

struct DataServerConfig
{
    uint32_t port = 3307;
    
    std::string mysql_host = "127.0.0.1";
    uint32_t mysql_port = 3306;
    std::string mysql_user = "root";
    std::string mysql_password = "password";
    std::string mysql_database = "tinydb";
    uint32_t mysql_max_connections = 10;
    uint32_t mysql_connection_timeout = 5;
    uint32_t mysql_read_timeout = 30;
    uint32_t mysql_write_timeout = 30;

    std::string log_dir = "logs";
    std::string log_file = "dataserver.log";
    std::string console_level = "info";
    std::string file_level = "info";
};

class ConfigManager : public Singleton<ConfigManager>
{
public:
    bool loadConfig(const std::string& config_file);
    const DataServerConfig& getConfig() const;

private:
    DataServerConfig config_;
};

} // namespace cncpp

#define sDataConfig cncpp::ConfigManager::getMe().getConfig()

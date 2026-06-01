#include "config_manager.h"
#include <fstream>
#include <sstream>
#include "logger.h"


namespace cncpp
{

    bool ConfigManager::loadConfig(const std::string& config_file)
    {
        std::ifstream file(config_file);
        if (!file.is_open())
        {
            LOG_WARN("Config file not found: {}, using defaults", config_file);
            return true;
        }

        std::string line;
        while (std::getline(file, line))
        {
            size_t pos = line.find('=');
            if (pos == std::string::npos)
                continue;

            std::string key   = line.substr(0, pos);
            std::string value = line.substr(pos + 1);

            if (key == "port")
                config_.port = std::stoi(value);
            else if (key == "mysql_host")
                config_.mysql_host = value;
            else if (key == "mysql_port")
                config_.mysql_port = std::stoi(value);
            else if (key == "mysql_user")
                config_.mysql_user = value;
            else if (key == "mysql_password")
                config_.mysql_password = value;
            else if (key == "mysql_database")
                config_.mysql_database = value;
            else if (key == "mysql_max_connections")
                config_.mysql_max_connections = std::stoi(value);
            else if (key == "mysql_connection_timeout")
                config_.mysql_connection_timeout = std::stoi(value);
            else if (key == "mysql_read_timeout")
                config_.mysql_read_timeout = std::stoi(value);
            else if (key == "mysql_write_timeout")
                config_.mysql_write_timeout = std::stoi(value);
            else if (key == "log_dir")
                config_.log_dir = value;
            else if (key == "log_file")
                config_.log_file = value;
            else if (key == "console_level")
                config_.console_level = value;
            else if (key == "file_level")
                config_.file_level = value;
        }

        LOG_INFO("Config loaded from: {}", config_file);
        return true;
    }

    const DataServerConfig& ConfigManager::getConfig() const
    {
        return config_;
    }

}  // namespace cncpp

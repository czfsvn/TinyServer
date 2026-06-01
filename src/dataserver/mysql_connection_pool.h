#pragma once

#include <mysql/mysql.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

namespace cncpp
{

    class MySQLConnectionPool
    {
    public:
        struct ConnectionConfig
        {
            std::string host;
            uint32_t    port;
            std::string user;
            std::string password;
            std::string database;
            uint32_t    max_connections;
            uint32_t    connection_timeout;
            uint32_t    read_timeout;
            uint32_t    write_timeout;
        };

        MySQLConnectionPool();
        ~MySQLConnectionPool();

        bool                   init(const ConnectionConfig& config);
        std::shared_ptr<MYSQL> acquire();
        void                   release(std::shared_ptr<MYSQL> conn);
        void                   close();
        size_t                 getPoolSize() const;
        size_t                 getActiveConnections() const;

    private:
        MYSQL* createConnection();
        bool   pingConnection(MYSQL* conn);

        ConnectionConfig    config_;
        std::queue<MYSQL*>  pool_;
        mutable std::mutex  mutex_;
        std::atomic<size_t> active_connections_{0};
        bool                initialized_{false};
    };

}  // namespace cncpp

#define sMySQLPool cncpp::Singleton<cncpp::MySQLConnectionPool>::getMe()

#include "mysql_connection_pool.h"
#include "logger.h"

namespace cncpp
{

    MySQLConnectionPool::MySQLConnectionPool()
    {
    }

    MySQLConnectionPool::~MySQLConnectionPool()
    {
        close();
    }

    bool MySQLConnectionPool::init(const ConnectionConfig& config)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (initialized_)
        {
            LOG_WARN("MySQLConnectionPool already initialized");
            return true;
        }

        config_ = config;
        mysql_library_init(0, nullptr, nullptr);

        for (uint32_t i = 0; i < config.max_connections; ++i)
        {
            MYSQL* conn = createConnection();
            if (conn)
            {
                pool_.push(conn);
                LOG_DEBUG("Created MySQL connection: {}", i + 1);
            }
            else
            {
                LOG_ERROR("Failed to create MySQL connection: {}", i + 1);
                close();
                return false;
            }
        }

        initialized_ = true;
        LOG_INFO("MySQLConnectionPool initialized with {} connections", pool_.size());
        return true;
    }

    MYSQL* MySQLConnectionPool::createConnection()
    {
        MYSQL* conn = mysql_init(nullptr);
        if (!conn)
        {
            LOG_ERROR("mysql_init failed");
            return nullptr;
        }

        my_bool reconnect = 1;
        mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);
        mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &config_.connection_timeout);
        mysql_options(conn, MYSQL_OPT_READ_TIMEOUT, &config_.read_timeout);
        mysql_options(conn, MYSQL_OPT_WRITE_TIMEOUT, &config_.write_timeout);

        if (!mysql_real_connect(conn, config_.host.c_str(), config_.user.c_str(), config_.password.c_str(),
                                config_.database.c_str(), config_.port, nullptr, 0))
        {
            LOG_ERROR("mysql_real_connect failed: {}", mysql_error(conn));
            mysql_close(conn);
            return nullptr;
        }

        LOG_DEBUG("MySQL connection established to {}:{}/{}", config_.host, config_.port, config_.database);
        return conn;
    }

    bool MySQLConnectionPool::pingConnection(MYSQL* conn)
    {
        if (mysql_ping(conn) != 0)
        {
            LOG_ERROR("MySQL connection ping failed: {}", mysql_error(conn));
            return false;
        }
        return true;
    }

    std::shared_ptr<MYSQL> MySQLConnectionPool::acquire()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        while (!pool_.empty())
        {
            MYSQL* conn = pool_.front();
            pool_.pop();

            if (pingConnection(conn))
            {
                active_connections_++;
                LOG_DEBUG("Acquired MySQL connection, active: {}", active_connections_.load());
                return std::shared_ptr<MYSQL>(conn, [this](MYSQL* c) {
                    this->release(std::shared_ptr<MYSQL>(c, [](MYSQL*) {
                    }));
                });
            }
            else
            {
                LOG_WARN("Connection invalid, creating new one");
                mysql_close(conn);
                conn = createConnection();
                if (conn)
                {
                    active_connections_++;
                    return std::shared_ptr<MYSQL>(conn, [this](MYSQL* c) {
                        this->release(std::shared_ptr<MYSQL>(c, [](MYSQL*) {
                        }));
                    });
                }
            }
        }

        LOG_ERROR("No available MySQL connections");
        return nullptr;
    }

    void MySQLConnectionPool::release(std::shared_ptr<MYSQL> conn)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (conn)
        {
            pool_.push(conn.get());
            conn.reset();
            active_connections_--;
            LOG_DEBUG("Released MySQL connection, active: {}", active_connections_.load());
        }
    }

    void MySQLConnectionPool::close()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        while (!pool_.empty())
        {
            MYSQL* conn = pool_.front();
            pool_.pop();
            mysql_close(conn);
        }

        mysql_library_end();
        initialized_        = false;
        active_connections_ = 0;
        LOG_INFO("MySQLConnectionPool closed");
    }

    size_t MySQLConnectionPool::getPoolSize() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size();
    }

    size_t MySQLConnectionPool::getActiveConnections() const
    {
        return active_connections_.load();
    }

}  // namespace cncpp

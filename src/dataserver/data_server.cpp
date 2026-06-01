#include "data_server.h"
#include "config_manager.h"
#include "data_processor.h"
#include "data_task_manager.h"
#include "logger.h"

DataServer::DataServer()
{
}

DataServer::~DataServer()
{
    onStop();
}

bool DataServer::onInit()
{
    LOG_INFO("DataServer initializing...");

    sDataConfig.loadConfig("config/dataserver.ini");

    if (!initMySQLPool())
    {
        LOG_ERROR("Failed to initialize MySQL connection pool");
        return false;
    }

    if (!sDataProcessor.init())
    {
        LOG_ERROR("Failed to initialize data processor");
        return false;
    }

    if (!initAcceptor())
    {
        LOG_ERROR("Failed to initialize acceptor");
        return false;
    }

    LOG_INFO("DataServer initialized successfully");
    return true;
}

bool DataServer::initMySQLPool()
{
    cncpp::MySQLConnectionPool::ConnectionConfig config;
    config.host               = sDataConfig.mysql_host;
    config.port               = sDataConfig.mysql_port;
    config.user               = sDataConfig.mysql_user;
    config.password           = sDataConfig.mysql_password;
    config.database           = sDataConfig.mysql_database;
    config.max_connections    = sDataConfig.mysql_max_connections;
    config.connection_timeout = sDataConfig.mysql_connection_timeout;
    config.read_timeout       = sDataConfig.mysql_read_timeout;
    config.write_timeout      = sDataConfig.mysql_write_timeout;

    LOG_INFO("Initializing MySQL pool: {}:{}/{}", config.host, config.port, config.database);
    return sMySQLPool.init(config);
}

bool DataServer::initAcceptor()
{
    try
    {
        port_     = sDataConfig.port;
        acceptor_ = std::make_shared<cncpp::Acceptor>(
            getIoContext(), port_, std::bind(&DataServer::onConnectionCreated, this, std::placeholders::_1));
        LOG_INFO("Acceptor initialized on port {}", port_);
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to initialize acceptor: {}", e.what());
        return false;
    }
}

bool DataServer::onStart()
{
    if (!startAcceptor())
    {
        LOG_ERROR("Failed to start acceptor");
        return false;
    }

    LOG_INFO("DataServer started successfully on port {}", port_);
    return true;
}

bool DataServer::startAcceptor()
{
    if (acceptor_)
    {
        acceptor_->start();
        return true;
    }
    return false;
}

void DataServer::onStop()
{
    LOG_INFO("DataServer stopping...");

    stopAcceptor();
    closeAllSessions();
    sMySQLPool.close();

    LOG_INFO("DataServer stopped");
}

void DataServer::stopAcceptor()
{
    if (acceptor_)
    {
        acceptor_->stop();
        acceptor_.reset();
    }
}

void DataServer::closeAllSessions()
{
    sDataTaskManager.stopAllTasks();
    LOG_INFO("All data tasks stopped");
}

bool DataServer::onTick()
{
    static uint32_t tick_count = 0;
    if (++tick_count % 100 == 0)
    {
        LOG_DEBUG("DataServer tick, active connections: {}, pool size: {}", sDataTaskManager.getActiveTaskCount(),
                  sMySQLPool.getActiveConnections());
    }
    return true;
}

void DataServer::onConnectionCreated(tcp::socket&& sock)
{
    auto task = sDataTaskManager.addTask(std::move(sock));
    if (task)
    {
        task->start();
        LOG_INFO("New connection created, task_id: {}, client_ip: {}", task->getTaskID(), task->getClientIP());
    }
}

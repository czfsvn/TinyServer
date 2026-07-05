#include "data_server.h"
#include "config_manager.h"
#include "data_processor.h"
#include "data_task_manager.h"
#include "io_context_pool.h"
#include "logger.h"
#include "mysql_conn.h"

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

    MySqlConnectPool::getMe().init(sMysqlConfig);

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

bool DataServer::initAcceptor()
{
    try
    {
        acceptor_ = sIOContextPool.createAcceptor(
            sDataServerConfig.listen_port(), std::bind(&DataServer::onConnectionCreated, this, std::placeholders::_1));

        LOG_INFO("Acceptor initialized on port {}", sDataServerConfig.listen_port());
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

    checkMysql();

    LOG_INFO("DataServer started successfully on port {}", sDataServerConfig.listen_port());
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
        LOG_DEBUG("DataServer tick, active connections: {}", sDataTaskManager.getActiveTaskCount());
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

void DataServer::checkMysql()
{
    ScopedMySqlConn con;
    const uint32_t  max_pack_size = con->getMaxMysqlPacketSize();
    LOG_INFO("MySQL connection max_pack_size={}", max_pack_size);
}

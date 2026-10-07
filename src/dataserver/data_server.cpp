#include "data_server.h"
#include "config.h"
#include "config_manager.h"
#include "data_processor.h"
#include "data_task_manager.h"
#include "io_context_pool.h"
#include "logger.h"
#include "mysql_conn.h"

#include <string>
#include <vector>
#include "all_table_metas.h"


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

    if (!checkMysql())
    {
        LOG_ERROR("MySQL is unreachable, refuse to start");
        return false;
    }

    if (!syncTableSchema())
    {
        LOG_ERROR("Table schema sync failed, refuse to start");
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

void DataServer::onTick()
{
    // 本拍到达的消息全部在这一拍交给业务。业务与定时器回调共享"无需加锁"
    // 这个前提，前提成立靠的就是它们在同一拍内串行发生（ADR-0004）。
    //
    // 抽干收口在 TaskManager 基类：三个服务端原本各有一份逐字重复的实现，加了
    // 每 task 上限之后更容易改一处漏两处。消息一律交回 DataTask::processMessage()
    // 自己分发，server 层不再插手。
    sDataTaskManager.drainInboundMessages(
        sMainConfig.max_messages_per_task_per_tick(), sMainConfig.max_messages_per_tick());

    static uint32_t tick_count = 0;
    if (++tick_count % 100 == 0)
    {
        LOG_DEBUG("DataServer tick, active connections: {}", sDataTaskManager.getActiveTaskCount());
    }
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

namespace
{
    /// 执行一条 DDL；失败时记日志并返回 false，不抛给调用方
    bool execDdl(cncpp::MysqlConn& conn, const std::string& sql)
    {
        try
        {
            mysqlpp::Query query = conn.getConn().query();
            query << sql;
            query.execute();
            return true;
        }
        catch (const mysqlpp::Exception& e)
        {
            LOG_ERROR("[SchemaSync] DDL failed: {}, sql={}", e.what(), sql);
            return false;
        }
    }

    bool tableExists(cncpp::MysqlConn& conn, const std::string& name)
    {
        mysqlpp::Query query = conn.getConn().query();
        query << "SELECT 1 FROM information_schema.TABLES"
              << " WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = " << mysqlpp::quote << name << " LIMIT 1";
        return query.store().num_rows() > 0;
    }

    bool columnExists(cncpp::MysqlConn& conn, const std::string& table, const std::string& column)
    {
        mysqlpp::Query query = conn.getConn().query();
        query << "SELECT 1 FROM information_schema.COLUMNS"
              << " WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = " << mysqlpp::quote << table
              << " AND COLUMN_NAME = " << mysqlpp::quote << column << " LIMIT 1";
        return query.store().num_rows() > 0;
    }
}  // namespace

bool DataServer::checkMysql()
{
    try
    {
        ScopedMySqlConn con;
        const uint32_t  max_pack_size = con->getMaxMysqlPacketSize();
        LOG_INFO("MySQL connection max_pack_size={}", max_pack_size);
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("MySQL unreachable: {}", e.what());
        return false;
    }
}

bool DataServer::syncTableSchema()
{
    const std::vector<const cncpp::db::TableMeta*>& metas = cncpp::db::allTableMetas();
    if (metas.empty())
    {
        LOG_WARN("[SchemaSync] tables.xml declares no table, skip");
        return true;
    }

    try
    {
        ScopedMySqlConn con;

        for (const cncpp::db::TableMeta* meta : metas)
        {
            if (!tableExists(*con, meta->name))
            {
                const std::string sql = cncpp::db::createTableSql(*meta);
                LOG_INFO("[SchemaSync] creating table {}: {}", meta->name, sql);
                if (!execDdl(*con, sql))
                {
                    LOG_ERROR("[SchemaSync] failed to create table {}", meta->name);
                }
                // 刚建出来的表列是全的，不必再逐列补
                continue;
            }

            for (std::size_t i = 0; i < meta->column_count; ++i)
            {
                const cncpp::db::ColumnMeta& column = meta->columns[i];
                if (columnExists(*con, meta->name, column.name))
                    continue;

                const std::string sql = cncpp::db::addColumnSql(*meta, column);
                LOG_INFO("[SchemaSync] adding column {}.{}: {}", meta->name, column.name, sql);
                if (!execDdl(*con, sql))
                {
                    LOG_ERROR("[SchemaSync] failed to add column {}.{}", meta->name, column.name);
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        // 连不上、information_schema 查不动，都拿不到「现在有什么」，也就无从判断该补什么
        LOG_ERROR("[SchemaSync] aborted: {}", e.what());
        return false;
    }
}

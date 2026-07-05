#include "mysql_conn.h"
#include "utils.h"

namespace cncpp
{
    MysqlConn::MysqlConn(const cncpp::MysqlConfig& config)
    {
        if (!m_conn.connect(config.database().c_str(), config.host().c_str(), config.user().c_str(),
                            config.password().c_str(), config.port()))
        {
            LOG_CRITICAL("Connection failed: {}", m_conn.error());
            return;
        }

        // MySQL++ 3.x 没有 set_charset 方法，使用 SQL 语句设置字符集
        try
        {
            m_conn.query("SET NAMES 'utf8mb4'").execute();
        }
        catch (const mysqlpp::Exception& e)
        {
            LOG_WARN("Failed to set charset: {}", e.what());
        }
    }

    MysqlConn::~MysqlConn()
    {
        if (m_inTransaction)
        {
            try
            {
                rollback();
            }
            catch (...)
            {
            }
        }
        m_conn.disconnect();
    }

    void MysqlConn::checkConnection()
    {
        if (!m_conn.connected())
        {
            throw MySQLException("Connection lost");
        }
    }

    size_t MysqlConn::execute(const std::string& sql, const mysqlpp::SQLTypeAdapter& params)
    {
        checkConnection();
        try
        {
            mysqlpp::Query query = m_conn.query(sql);
            if (!params.is_null())
            {
                query << params;
            }
            return query.execute().rows();
        }
        catch (const mysqlpp::BadQuery& e)
        {
            handleSQLError(e, sql);
            return 0;
        }
    }

    void MysqlConn::beginTransaction()
    {
        checkConnection();
        m_conn.query("START TRANSACTION").execute();
        m_inTransaction = true;
    }

    void MysqlConn::commit()
    {
        checkConnection();
        m_conn.query("COMMIT").execute();
        m_inTransaction = false;
    }

    void MysqlConn::rollback()
    {
        checkConnection();
        m_conn.query("ROLLBACK").execute();
        m_inTransaction = false;
    }

    void MysqlConn::handleSQLError(const mysqlpp::BadQuery& e, const std::string& sql)
    {
        throw MySQLException(std::string("SQL Error: ") + e.what() + " (Code: " + std::to_string(e.errnum()) + ")",
                             sql);
    }

    uint32_t MysqlConn::getMaxMysqlPacketSize()
    {
        mysqlpp::Query            query = m_conn.query("SHOW VARIABLES LIKE 'max_allowed_packet'");
        mysqlpp::StoreQueryResult res   = query.store();
        if (res && res.num_rows() == 1)
            return cast_to<uint32_t>(res[0]["Value"].c_str());

        return 0;
    }

    uint32_t MysqlConn::getMaxSafeMysqlPacketSize()
    {
        static const uint32_t surplus_size = 1000;
        const uint32_t        max_size     = getMaxMysqlPacketSize();
        if (!max_size || max_size <= surplus_size)
            return 0;

        return max_size - surplus_size;
    }
}  // namespace cncpp

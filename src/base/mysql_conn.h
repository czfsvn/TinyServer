#pragma once

#include <mysql++.h>
#include <vector>

#include "config.h"
#include "connection_pool.h"
#include "logger.h"

namespace cncpp
{
    class MySQLException : public std::runtime_error
    {
    public:
        MySQLException(const std::string& msg, const std::string& sql = "")
            : std::runtime_error(msg + " [SQL: " + sql + "]")
        {
        }
    };

    class MysqlConn
    {
    public:
    public:
        bool isConnected()
        {
            return true;
        }

        // 初始化连接（RAII）
        explicit MysqlConn(const cncpp::MysqlConfig& config);
        ~MysqlConn();

        uint32_t getMaxMysqlPacketSize();
        uint32_t getMaxSafeMysqlPacketSize();

        // 执行查询，返回结果集（泛型支持）
        template <typename T = std::vector<std::string>>
        std::vector<T> query(const std::string& sql, const mysqlpp::SQLTypeAdapter& params = "")
        {
            checkConnection();
            try
            {
                mysqlpp::Query query = m_conn.query(sql);
                if (!params.is_null())
                {
                    query << " " << params;
                }

                mysqlpp::StoreQueryResult res = query.store();
                std::vector<T>            result;

                for (const auto& row : res)
                {
                    result.push_back(row);
                }
                return result;
            }
            catch (const mysqlpp::BadQuery& e)
            {
                handleSQLError(e, sql);
                return {};  // 避免编译器警告
            }
        }

        template <typename DB>
        std::vector<DB> loadWhere(const std::string& where)
        {
            mysqlpp::Query query = m_conn.query();

            try
            {
                query << "SELECT " << DB::field_list() << " FROM " << DB::table();
                if (where.size())
                {
                    query << " WHERE " << where;
                }

                std::vector<DB> res;
                query.storein(res);
                return res;
            }
            catch (const mysqlpp::BadQuery& er)
            {
                LOG_ERROR("[MysqlConn][loadWhere] BadQuery err: {}, query={}", er.what(), query.str());
                return {};
            }
            catch (const mysqlpp::BadConversion& er)
            {
                LOG_ERROR(
                    "[MysqlConn][loadWhere] Conversion error: {}, retrieved data size: {}, actual "
                    "size: {}, , query={}",
                    er.what(), er.retrieved, er.actual_size, query.str());
                return {};
            }
            catch (const mysqlpp::Exception& er)
            {
                LOG_ERROR("[MysqlConn][loadWhere] Error: {}, , query={}", er.what(), query.str());
                return {};
            }

            return {};
        }

        template <typename DB, typename Container>
        uint64_t replaceAll(const Container& cont)
        {
            if (!cont.size())
                return 0;

            const uint32_t max_safe_packet_size = getMaxSafeMysqlPacketSize();
            if (!max_safe_packet_size)
                return 0;

            mysqlpp::Query query = m_conn.query();

            try
            {
                const std::string field_str = DB::field_list();
                query << "REPLACE INTO `" << DB::table() << "` (" << DB::field_list() << ") VALUES";
                const std::string head_str = query.str();

                uint64_t saved_size = 0;
                uint32_t readycount = 0;
                for (const auto& item : cont)
                {
                    if (readycount)
                        query << ",";

                    readycount++;
                    query << "(" << item.value_list() << ")";
                    if (query.str().size() > max_safe_packet_size)
                    {
                        mysqlpp::SimpleResult res = query.execute();
                        if (!res)
                        {
                            LOG_ERROR("[MysqlConn][replaceAll] error, effectnum={}, query={}", saved_size, query.str());
                            return saved_size;
                        }

                        saved_size += res.rows();

                        query = m_conn.query();
                        query << head_str;
                        readycount = 0;
                    }
                }

                if (readycount)
                {
                    mysqlpp::SimpleResult res = query.execute();
                    if (!res)
                    {
                        LOG_ERROR("[MysqlConn][replaceAll] error, effectnum={}, query={}", saved_size, query.str());
                        return saved_size;
                    }

                    saved_size += res.rows();
                }

                return saved_size;
            }
            catch (const mysqlpp::BadQuery& er)
            {
                LOG_ERROR("[MysqlConn][replaceAll] BadQuery err: {}, query={}", er.what(), query.str());
                return -1;
            }
            catch (const mysqlpp::BadConversion& er)
            {
                LOG_ERROR(
                    "[MysqlConn][replaceAll] Conversion error: {}, retrieved data size: {}, actual "
                    "size: {}, , query={}",
                    er.what(), er.retrieved, er.actual_size, query.str());
                return -1;
            }
            catch (const mysqlpp::Exception& er)
            {
                LOG_ERROR("[MysqlConn][replaceAll] Error: {}, , query={}", er.what(), query.str());
                return -1;
            }

            return 0;
        }

        template <typename DB>
        uint64_t replaceDB(const DB& data)
        {
            mysqlpp::Query query = m_conn.query();

            try
            {
                const std::string field_str = DB::field_list();
                query << "REPLACE INTO `" << DB::table() << "` (" << DB::field_list() << ") VALUES";
                // const std::string head_str = query.str();
                query << "(" << data.value_list() << ")";

                mysqlpp::SimpleResult res = query.execute();
                if (!res)
                {
                    LOG_ERROR("[MysqlConn][replaceOne] error, query={}", query.str());
                    return 0;
                }

                return res.rows();
            }
            catch (const mysqlpp::BadQuery& er)
            {
                LOG_ERROR("[MysqlConn][replaceOne] BadQuery err: {}, query={}", er.what(), query.str());
                return -1;
            }
            catch (const mysqlpp::BadConversion& er)
            {
                LOG_ERROR(
                    "[MysqlConn][replaceOne] Conversion error: {}, retrieved data size: {}, actual "
                    "size: {}, , query={}",
                    er.what(), er.retrieved, er.actual_size, query.str());
                return -1;
            }
            catch (const mysqlpp::Exception& er)
            {
                LOG_ERROR("[MysqlConn][replaceAll] Error: {}, , query={}", er.what(), query.str());
                return -1;
            }

            return 0;
        }

        template <typename DB>
        uint64_t deleteWhere(const std::string& where)
        {
            mysqlpp::Query query = m_conn.query();

            try
            {
                const std::string field_str = DB::field_list();
                query << "DELETE FROM `" << DB::table() << "`";
                if (where.size())
                    query << " where " << where;

                mysqlpp::SimpleResult res = query.execute();
                if (!res)
                {
                    LOG_ERROR("[MysqlConn][deleteWhere] error, query={}", query.str());
                    return 0;
                }

                return res.rows();
            }
            catch (const mysqlpp::BadQuery& er)
            {
                LOG_ERROR("[MysqlConn][deleteWhere] BadQuery err: {}, query={}", er.what(), query.str());
                return -1;
            }
            catch (const mysqlpp::BadConversion& er)
            {
                LOG_ERROR(
                    "[MysqlConn][deleteWhere] Conversion error: {}, retrieved data size: {}, actual "
                    "size: {}, , query={}",
                    er.what(), er.retrieved, er.actual_size, query.str());
                return -1;
            }
            catch (const mysqlpp::Exception& er)
            {
                LOG_ERROR("[MysqlConn][deleteWhere] Error: {}, , query={}", er.what(), query.str());
                return -1;
            }

            return 0;
        }

        template <typename DB, typename Container>
        uint32_t deleteWhereByKeys(const std::string& keyname, const Container& cont)
        {
            if (keyname.empty || cont.empty())
                return 0;

            const uint32_t max_safe_packet_size = getMaxSafeMysqlPacketSize();
            if (!max_safe_packet_size)
                return 0;

            mysqlpp::Query query          = m_conn.query();
            uint32_t       afffected_rows = 0;

            try
            {
                const std::string field_str = DB::field_list();
                query << "DELETE FROM `" << DB::table() << "` WHERE " << keyname << " IN(";
                const std::string header_str = query.str();

                bool isfirst = true;
                for (const auto& key : cont)
                {
                    if (isfirst)
                    {
                        query << "`" << key << "`";
                        isfirst = false;
                    }
                    else
                    {
                        query << ",`" << key << "`";
                    }

                    if (query.str().size() >= max_safe_packet_size)
                    {
                        query << ")";

                        mysqlpp::SimpleResult res = query.execute();
                        if (!res)
                        {
                            LOG_ERROR("[MysqlConn][deleteWhereByKeys] error, query={}, afffected_rows={}", query.str(),
                                      afffected_rows);
                            return afffected_rows;
                        }

                        afffected_rows += res.rows();
                        query = m_conn.query();
                        query << header_str;
                        isfirst = true;
                    }
                }

                if (!isfirst)
                {
                    query << ")";

                    mysqlpp::SimpleResult res = query.execute();
                    if (!res)
                    {
                        LOG_ERROR("[MysqlConn][deleteWhereByKeys] error, query={}, afffected_rows={}", query.str(),
                                  afffected_rows);
                        return afffected_rows;
                    }

                    afffected_rows += res.rows();
                }

                return afffected_rows;
            }
            catch (const mysqlpp::BadQuery& er)
            {
                LOG_ERROR("[MysqlConn][deleteWhereByKeys] BadQuery err: {}, query={}", er.what(), query.str());
                return -1;
            }
            catch (const mysqlpp::BadConversion& er)
            {
                LOG_ERROR(
                    "[MysqlConn][deleteWhereByKeys] Conversion error: {}, retrieved data size: {}, actual "
                    "size: {}, , query={}",
                    er.what(), er.retrieved, er.actual_size, query.str());
                return -1;
            }
            catch (const mysqlpp::Exception& er)
            {
                LOG_ERROR("[MysqlConn][deleteWhereByKeys] Error: {}, , query={}", er.what(), query.str());
                return -1;
            }

            return 0;
        }

        // 执行更新（INSERT/UPDATE/DELETE）
        size_t execute(const std::string& sql, const mysqlpp::SQLTypeAdapter& params = {});

        // 事务管理
        void beginTransaction();
        void commit();
        void rollback();

        mysqlpp::Connection& getConn()
        {
            return m_conn;
        }

    private:
        mysqlpp::Connection m_conn;
        bool                m_inTransaction = false;

        void checkConnection();
        void handleSQLError(const mysqlpp::BadQuery& e, const std::string& sql);
    };
}  // namespace cncpp

using MySqlConnectPool = cncpp::MyConnPool<cncpp::MysqlConn, cncpp::MysqlConfig>;
using ScopedMySqlConn  = cncpp::MyScopedConn<cncpp::MysqlConn, cncpp::MysqlConfig>;
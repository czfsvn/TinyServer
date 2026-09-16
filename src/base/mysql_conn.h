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
        /// @brief 连接是否仍然建立着
        ///
        /// 弱信号：只说明「上一次通信之后连接没断」，测不出 TCP 半开、也测不出对端
        /// wait_timeout 已经把连接掐掉。连接池用它丢弃死连接并重建（见 MyConnPool::grab），
        /// 业务代码不要拿它当探活结果。
        bool isConnected();

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

                mysqlpp::StoreQueryResult rows = query.store();
                std::vector<DB>           res;
                res.reserve(rows.num_rows());
                for (const mysqlpp::Row& row : rows)
                {
                    res.push_back(DB::from_row(row));
                }
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
                query << "REPLACE INTO `" << DB::table() << "` (" << DB::field_list() << ") VALUES";
                const std::string head_str = query.str();

                uint64_t saved_size = 0;
                uint32_t readycount = 0;
                for (const auto& item : cont)
                {
                    if (readycount)
                        query << ",";

                    readycount++;
                    query << "(";
                    item.value_list(query);
                    query << ")";
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
                query << "REPLACE INTO `" << DB::table() << "` (" << DB::field_list() << ") VALUES";
                // const std::string head_str = query.str();
                query << "(";
                data.value_list(query);
                query << ")";

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

        // 注：原 deleteWhereByKeys() 已于 2026-09-11 删除。
        //   删除原因：经全仓库检索确认【从未被任何代码调用】（100% 死代码），
        //             且实现存在两处错误：
        //               1) keyname.empty 漏写括号（应为 empty()），函数体语法非法；
        //               2) 给「值」加反引号（反引号是标识符引用符，用于值上非法）。
        //             因是函数模板且从未实例化，上述错误始终未被编译器诊断。
        //   替代能力：由 xml2db 生成的 DAO 提供，
        //             见 src/database/dbtables/<table>.h 的 deleteByKey()。

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
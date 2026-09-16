/**
 * @file sql_value.h
 * @brief MysqlConn ORM 契约中 value_list() 的共享实现：把 C++ 值安全地写进 mysqlpp::Query
 * @author xml2db
 * @date 2026-09-16
 */

#pragma once

#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

#include <mysql++.h>

namespace cncpp
{
    namespace db
    {
        namespace detail
        {
            /**
             * @brief 把数值格式化成 SQL 字面量后写进查询流
             *
             * 不直接写 query << value 有两个原因：
             *   1) int8_t / uint8_t 本质是 char，直接进流会被当成字符输出；
             *   2) 统一转成 const char*，只命中 std::ostream 的确定性重载，
             *      不依赖 mysql++ 对算术类型 operator<< 的具体实现。
             */
            // 注意：这里必须「先写再 return query」，不能写成 return query << xxx;
            // std::ostream 的非成员 operator<< 返回的是 std::ostream&，基类引用无法隐式转回 mysqlpp::Query&
            template <typename T>
            inline mysqlpp::Query& writeNumber(mysqlpp::Query& query, const T& value)
            {
                std::ostringstream oss;
                oss << value;
                const std::string  text = oss.str();
                query << text.c_str();
                return query;
            }
        }  // namespace detail

        /**
         * @brief 字符串：交给 mysql++ 转义并加引号
         *
         * 这正是 value_list() 不能返回 std::string 的原因 —— 一旦先把整行拼成字符串
         * 再交给 Query，就丢掉了 mysql++ 的 quote/escape 保护，字符串主键会变成注入面。
         */
        inline mysqlpp::Query& sqlValue(mysqlpp::Query& query, const std::string& value)
        {
            query << mysqlpp::quote << value;
            return query;
        }

        inline mysqlpp::Query& sqlValue(mysqlpp::Query& query, const bool value)
        {
            return detail::writeNumber(query, static_cast<int>(value));
        }

        inline mysqlpp::Query& sqlValue(mysqlpp::Query& query, const int8_t value)
        {
            return detail::writeNumber(query, static_cast<int>(value));
        }

        inline mysqlpp::Query& sqlValue(mysqlpp::Query& query, const uint8_t value)
        {
            return detail::writeNumber(query, static_cast<unsigned int>(value));
        }

        inline mysqlpp::Query& sqlValue(mysqlpp::Query& query, const int16_t value)
        {
            return detail::writeNumber(query, static_cast<int>(value));
        }

        inline mysqlpp::Query& sqlValue(mysqlpp::Query& query, const uint16_t value)
        {
            return detail::writeNumber(query, static_cast<unsigned int>(value));
        }

        inline mysqlpp::Query& sqlValue(mysqlpp::Query& query, const int32_t value)
        {
            return detail::writeNumber(query, value);
        }

        inline mysqlpp::Query& sqlValue(mysqlpp::Query& query, const uint32_t value)
        {
            return detail::writeNumber(query, value);
        }

        inline mysqlpp::Query& sqlValue(mysqlpp::Query& query, const int64_t value)
        {
            return detail::writeNumber(query, value);
        }

        inline mysqlpp::Query& sqlValue(mysqlpp::Query& query, const uint64_t value)
        {
            return detail::writeNumber(query, value);
        }

        /**
         * @brief 浮点：std::ostream 默认精度只有 6 位有效数字，直接写会丢精度
         *
         * 固定用 max_digits10，保证写入的值能无损读回。
         */
        inline mysqlpp::Query& sqlValue(mysqlpp::Query& query, const float value)
        {
            std::ostringstream oss;
            oss << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
            const std::string  text = oss.str();
            query << text.c_str();
            return query;
        }

        inline mysqlpp::Query& sqlValue(mysqlpp::Query& query, const double value)
        {
            std::ostringstream oss;
            oss << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
            const std::string  text = oss.str();
            query << text.c_str();
            return query;
        }

        /**
         * @brief 取结果集一列的文本，NULL / 缺失时返回空串
         *
         * mysqlpp::String::c_str() 对 NULL 列可能返回空指针，直接构造 std::string 会崩，
         * 这里统一兜底。
         */
        inline const char* rowText(const mysqlpp::Row& row, const char* field)
        {
            const mysqlpp::String value = row[field];
            const char*           text  = value.c_str();
            return text ? text : "";
        }
    }  // namespace db
}  // namespace cncpp

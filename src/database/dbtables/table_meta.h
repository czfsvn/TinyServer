/**
 * @file table_meta.h
 * @brief 表元信息（Meta）与由它派生 DDL / 字段列表的 helper
 * @author 手写（与 sql_value.h 同为所有生成物共享的 helper，不生成、也不被 clean 删除）
 * @date 2026-09-16
 */

#pragma once

#include <cstddef>
#include <string>

namespace cncpp
{
    namespace db
    {
        /**
         * @brief Meta 中的一列
         *
         * 只描述 DDL 里真正生成的那些属性。不描述 DEFAULT —— tables.xml 不携带默认值，
         * 补上就是替业务编语义。
         */
        struct ColumnMeta
        {
            const char* name;         ///< 列名
            const char* type;         ///< MySQL 列类型，如 "VARCHAR(255)"
            const char* comment;      ///< 列注释，无注释时为空串（不是 nullptr）
            bool        not_null;     ///< 是否 NOT NULL（XML 不携带 nullable，恒为 true）
            bool        primary_key;  ///< 是否属于主键
        };

        /**
         * @brief Meta 中的一张表
         *
         * columns 按 tables.xml 的 index 排序，与 DDL 的列顺序一致。
         */
        struct TableMeta
        {
            const char*       name;          ///< 表名
            const char*       engine;        ///< 存储引擎，取自生成器常量 DEFAULT_ENGINE
            const char*       charset;       ///< 表字符集，取自生成器常量 DEFAULT_CHARSET
            const char*       comment;       ///< 表注释，无注释时为空串
            const ColumnMeta* columns;       ///< 列数组
            std::size_t       column_count;  ///< 列数
        };

        namespace detail
        {
            /// 注释里的 \ 与 ' 必须转义，否则拼出来的 DDL 会被截断或注入
            inline std::string escapeComment(const std::string& text)
            {
                std::string out;
                out.reserve(text.size());
                for (const char ch : text)
                {
                    if (ch == '\\')
                        out += "\\\\";
                    else if (ch == '\'')
                        out += "''";
                    else
                        out.push_back(ch);
                }
                return out;
            }
        }  // namespace detail

        /**
         * @brief 单列的 DDL 片段：\`name\` TYPE [NOT NULL] [COMMENT '...']
         *
         * 供 createTableSql() 与 addColumnSql() 共用，保证建表与加列拼出来的列定义一致。
         */
        inline std::string columnDefSql(const ColumnMeta& column)
        {
            std::string sql = "`";
            sql += column.name;
            sql += "` ";
            sql += column.type;

            if (column.not_null)
                sql += " NOT NULL";

            if (column.comment && *column.comment)
            {
                sql += " COMMENT '";
                sql += detail::escapeComment(column.comment);
                sql += "'";
            }
            return sql;
        }

        /**
         * @brief 由 Meta 派生 CREATE TABLE
         *
         * 与 src/database/sql/<table>.sql 语义一致（后者仅供人工审阅，不参与运行时），
         * 但不对齐列宽 —— 运行时产物没必要为可读性付出对齐成本。
         */
        inline std::string createTableSql(const TableMeta& meta)
        {
            std::string sql = "CREATE TABLE IF NOT EXISTS `";
            sql += meta.name;
            sql += "` (\n";

            for (std::size_t i = 0; i < meta.column_count; ++i)
            {
                if (i)
                    sql += ",\n";
                sql += "    ";
                sql += columnDefSql(meta.columns[i]);
            }

            // 主键列按 Meta 里的列序拼：information_schema 不给列序，但建表时我们是按自己的列序写的
            std::string keys;
            for (std::size_t i = 0; i < meta.column_count; ++i)
            {
                if (!meta.columns[i].primary_key)
                    continue;
                if (!keys.empty())
                    keys += ", ";
                keys += "`";
                keys += meta.columns[i].name;
                keys += "`";
            }
            if (!keys.empty())
            {
                sql += ",\n    PRIMARY KEY (";
                sql += keys;
                sql += ")";
            }

            sql += "\n) ENGINE=";
            sql += meta.engine;
            sql += " DEFAULT CHARSET=";
            sql += meta.charset;
            if (meta.comment && *meta.comment)
            {
                sql += " COMMENT='";
                sql += detail::escapeComment(meta.comment);
                sql += "'";
            }
            sql += ";";
            return sql;
        }

        /**
         * @brief 补缺用：给已存在的表加一列（见 ADR-005 D3）
         *
         * 只用于「表在、列不在」。已存在的列一律不动。
         */
        inline std::string addColumnSql(const TableMeta& meta, const ColumnMeta& column)
        {
            std::string sql = "ALTER TABLE `";
            sql += meta.name;
            sql += "` ADD COLUMN ";
            sql += columnDefSql(column);
            sql += ";";
            return sql;
        }

        /**
         * @brief SELECT / REPLACE 共用的字段列表：\`a\`,\`b\`,\`c\`
         *
         * 行类型的 field_list() 由它派生，因此列名只在 Meta 里存一份。
         */
        inline std::string fieldListSql(const TableMeta& meta)
        {
            std::string out;
            for (std::size_t i = 0; i < meta.column_count; ++i)
            {
                if (i)
                    out += ",";
                out += "`";
                out += meta.columns[i].name;
                out += "`";
            }
            return out;
        }
    }  // namespace db
}  // namespace cncpp

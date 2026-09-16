/**
 * @file stock.h
 * @brief 表 Stock 的行类型与数据访问接口
 * @author xml2db（由 tinytools/xml2db.py 从 src/database/dbtables/tables.xml 生成，请勿手工修改）
 * @date 2026-09-16
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <mysql++.h>

#include "table_meta.h"

namespace cncpp
{
    namespace db
    {

        /**
         * @brief 表 Stock 的一行数据，并承载该表的数据访问接口
         *
         * 成员按「构造函数 / getter / 成员函数 / 静态函数 / 数据成员」分区，便于按类别定位。
         *
         * 数据成员私有、只经 getter 读出：对象一经构造即只读，整行写入靠重新构造而非逐字段修改。
         *
         * ORM 契约：table() / field_list() / value_list(Query&) / from_row(Row)。
         *
         * Meta：meta() 给出本表的结构（列名 / MySQL 类型 / NOT NULL / 主键），
         * 建表语句与字段列表都由它派生，因此列名只在生成物里存一份。
         *
         * 数据访问接口全部是静态函数：连接由函数内部从连接池取（ScopedMySqlConn），
         * 调用方无需持有、也无需传入 MysqlConn。
         *
         * WHERE 条件的拼接与转义全部收在 stock.cpp 里，裸 where 字符串不对外暴露。
         */
        struct Stock
        {
        public:
            // ---------- 构造函数 ----------
            /** @brief 默认构造：各字段取类型零值，主要供 from_row() 与容器使用 */
            Stock() = default;

            /** @brief 逐字段构造 */
            Stock(const std::string& item_id, uint64_t num, double weight, float price, const std::string& description);

            // ---------- getter ----------
            /** @brief 物品id */
            const std::string& getItemId() const;

            /** @brief 数量 */
            uint64_t getNum() const;

            /** @brief 权重 */
            double getWeight() const;

            /** @brief 价格 */
            float getPrice() const;

            /** @brief 描述 */
            const std::string& getDescription() const;

            // ---------- 成员函数 ----------
            /**
             * @brief 把整行的值写进查询流，供 REPLACE INTO ... VALUES (...) 使用
             * @param query 已绑定连接的 mysql++ 查询流，字符串需要靠它做转义
             */
            void value_list(mysqlpp::Query& query) const;

            // ---------- 静态函数 ----------
            /** @brief 本表的 Meta：建表语句与字段列表都由它派生（见 ADR-005） */
            static const TableMeta& meta();

            /** @brief 表名 */
            static const char* table();

            /** @brief SELECT / REPLACE 共用的字段列表 */
            static const char* field_list();

            /** @brief 从结果集的一行构造对象 */
            static Stock from_row(const mysqlpp::Row& row);

            /** @brief 加载全表（无 LIMIT，仅适用于配置量级的表） */
            static std::vector<Stock> loadAll();

            /** @brief 按主键加载单行，未命中返回 std::nullopt */
            static std::optional<Stock> loadByKey(const std::string& item_id);

            /** @brief 整行写入（主键已存在则整行覆盖） */
            static uint64_t save(const Stock& row);

            /** @brief 批量整行写入，返回受影响行数 */
            static uint64_t saveAll(const std::vector<Stock>& rows);

            /** @brief 按主键删除 */
            static uint64_t deleteByKey(const std::string& item_id);

            /**
             * @brief 逃生口：复杂条件查询
             * @param where 直接拼进 WHERE 子句的条件，调用方自行保证其合法性
             */
            static std::vector<Stock> loadWhere(const std::string& where);

        private:
            // ---------- 数据成员 ----------
            std::string item_id;
            uint64_t    num = 0;
            double      weight = 0.0;
            float       price = 0.0f;
            std::string description;
        };
    }  // namespace db
}  // namespace cncpp

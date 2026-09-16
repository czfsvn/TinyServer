/**
 * @file stock.cpp
 * @brief 表 Stock 的行类型契约与数据访问接口的实现
 * @author xml2db（由 tinytools/xml2db.py 从 src/database/dbtables/tables.xml 生成，请勿手工修改）
 * @date 2026-09-16
 */

#include "stock.h"

#include "mysql_conn.h"
#include "sql_value.h"
#include "utils.h"

namespace cncpp
{
    namespace db
    {

        // ---------- 构造函数 ----------
        Stock::Stock(const std::string& item_id,
            uint64_t num,
            double weight,
            float price,
            const std::string& description)
            : item_id(item_id), num(num), weight(weight), price(price), description(description)
        {
        }

        // ---------- getter ----------
        const std::string& Stock::getItemId() const
        {
            return item_id;
        }

        uint64_t Stock::getNum() const
        {
            return num;
        }

        double Stock::getWeight() const
        {
            return weight;
        }

        float Stock::getPrice() const
        {
            return price;
        }

        const std::string& Stock::getDescription() const
        {
            return description;
        }

        // ---------- 成员函数 ----------
        void Stock::value_list(mysqlpp::Query& query) const
        {
            sqlValue(query, item_id);
            query << ",";
            sqlValue(query, num);
            query << ",";
            sqlValue(query, weight);
            query << ",";
            sqlValue(query, price);
            query << ",";
            sqlValue(query, description);
        }

        // ---------- 静态函数 ----------
        const TableMeta& Stock::meta()
        {
            static const ColumnMeta k_columns[] = {
                { "item_id"    , "VARCHAR(255)"   , "物品id", true, true },
                { "num"        , "BIGINT UNSIGNED", "数量", true, false },
                { "weight"     , "DOUBLE"         , "权重", true, false },
                { "price"      , "FLOAT"          , "价格", true, false },
                { "description", "VARCHAR(255)"   , "描述", true, false },
            };

            static const TableMeta k_meta = {
                "Stock", "InnoDB", "utf8mb4", "", k_columns, 5
            };

            return k_meta;
        }

        const char* Stock::table()
        {
            return meta().name;
        }

        const char* Stock::field_list()
        {
            // 由 Meta 派生；函数内 static 保证只拼一次，首次调用的初始化是线程安全的
            static const std::string fields = fieldListSql(meta());
            return fields.c_str();
        }

        Stock Stock::from_row(const mysqlpp::Row& row)
        {
            Stock item;
            item.item_id = rowText(row, "item_id");
            item.num = cast_to<uint64_t>(rowText(row, "num"), 0);
            item.weight = cast_to<double>(rowText(row, "weight"), 0.0);
            item.price = cast_to<float>(rowText(row, "price"), 0.0f);
            item.description = rowText(row, "description");
            return item;
        }

        std::vector<Stock> Stock::loadAll()
        {
            ScopedMySqlConn con;
            return con->loadWhere<Stock>("");
        }

        std::optional<Stock> Stock::loadByKey(const std::string& item_id)
        {
            ScopedMySqlConn con;
            mysqlpp::Query where = con->getConn().query();
            where << "`item_id` = ";
            sqlValue(where, item_id);

            std::vector<Stock> rows = con->loadWhere<Stock>(where.str());
            if (rows.empty())
            {
                return std::nullopt;
            }
            return rows.front();
        }

        uint64_t Stock::save(const Stock& row)
        {
            ScopedMySqlConn con;
            return con->replaceDB(row);
        }

        uint64_t Stock::saveAll(const std::vector<Stock>& rows)
        {
            ScopedMySqlConn con;
            return con->replaceAll<Stock>(rows);
        }

        uint64_t Stock::deleteByKey(const std::string& item_id)
        {
            ScopedMySqlConn con;
            mysqlpp::Query where = con->getConn().query();
            where << "`item_id` = ";
            sqlValue(where, item_id);

            return con->deleteWhere<Stock>(where.str());
        }

        std::vector<Stock> Stock::loadWhere(const std::string& where)
        {
            ScopedMySqlConn con;
            return con->loadWhere<Stock>(where);
        }
    }  // namespace db
}  // namespace cncpp

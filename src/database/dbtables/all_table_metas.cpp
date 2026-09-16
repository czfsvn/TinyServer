/**
 * @file all_table_metas.cpp
 * @brief 全部表 Meta 的清单定义
 * @author xml2db（由 tinytools/xml2db.py 从 src/database/dbtables/tables.xml 生成，请勿手工修改）
 * @date 2026-09-16
 */

#include "all_table_metas.h"

#include "stock.h"

namespace cncpp
{
    namespace db
    {

        const std::vector<const TableMeta*>& allTableMetas()
        {
            static const std::vector<const TableMeta*> k_all = {
                &Stock::meta(),
            };
            return k_all;
        }
    }  // namespace db
}  // namespace cncpp

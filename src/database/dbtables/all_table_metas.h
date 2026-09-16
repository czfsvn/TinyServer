/**
 * @file all_table_metas.h
 * @brief 全部表 Meta 的清单
 * @author xml2db（由 tinytools/xml2db.py 从 src/database/dbtables/tables.xml 生成，请勿手工修改）
 * @date 2026-09-16
 */

#pragma once

#include <vector>

#include "table_meta.h"

namespace cncpp
{
    namespace db
    {

        /**
         * @brief 全部表的 Meta，顺序与 tables.xml 一致
         *
         * 由生成器显式枚举（见 ADR-005 D5）：不用自注册，避免静态库链接时被静默丢弃。
         */
        const std::vector<const TableMeta*>& allTableMetas();
    }  // namespace db
}  // namespace cncpp

#!/usr/bin/env python3
"""xml2db：读 src/database/dbtables/tables.xml，生成 MySQL DDL 与 C++ 访问层。

单文件实现，按以下顺序组织：

    1. 类型映射     C++ 类型 -> MySQL 类型、SQL 字面量 / 反序列化表达式
    2. 表定义解析   tables.xml 的解析与校验
    3. DDL 渲染
    4. C++ 渲染     行类型 + 数据访问接口 + Meta（每表一对 .h / .cpp）
    4.5 Meta 清单    all_table_metas.h / .cpp（只列表名，供启动时补缺）
    5. CLI 入口

用法（在仓库任意位置执行均可，路径默认相对仓库根解析）：

    python tinytools/xml2db.py
    python tinytools/xml2db.py --xml <path> --sql-out <dir> --cpp-out <dir>

产物（每张表两个文件，声明与实现分离）：

    <sql-out>/tables.sql           全部表的 CREATE TABLE（默认）
    <sql-out>/<table>.sql          每张表一个文件（--sql-per-table）
    <cpp-out>/<table>.h            行类型 + 数据访问接口 + Meta 的声明
    <cpp-out>/<table>.cpp          行类型契约与数据访问接口 + Meta 的实现
    <cpp-out>/all_table_metas.h    全部表 Meta 的清单（声明）
    <cpp-out>/all_table_metas.cpp  全部表 Meta 的清单（只列表名，见 ADR-005 D5）

映射规则与产物约定详见 src/database/dbtables/ADR-002-db-static-accessors.md 与 CONTEXT.md。
"""

import argparse
import datetime
import os
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from typing import List, Optional

# =============================================================================
# 1. 类型映射：C++ 类型 -> MySQL 类型、SQL 字面量 / 反序列化表达式
#
#     std::string -> VARCHAR(255)（可用 XML 的 len 属性覆盖）
#     uint64_t    -> BIGINT UNSIGNED
#     double      -> DOUBLE
#     float       -> FLOAT
#
# 其中 FLOAT 存金额存在精度损失，是已知并接受的取舍（Stock 为配置表）。
# =============================================================================

# std::string 在 XML 中不携带长度信息时的默认宽度
DEFAULT_VARCHAR_LEN = 255

# 表级约定（见 ADR-001-db-codegen.md）：DDL 与 Meta 共用同一份常量，
# 避免「建表的语句」和「Meta 里记的结构」各写一遍后各改各的
DEFAULT_ENGINE = "InnoDB"
DEFAULT_CHARSET = "utf8mb4"

_MYSQL_TYPE_MAP = {
    "bool": "TINYINT(1)",
    "int8_t": "TINYINT",
    "uint8_t": "TINYINT UNSIGNED",
    "int16_t": "SMALLINT",
    "uint16_t": "SMALLINT UNSIGNED",
    "int32_t": "INT",
    "uint32_t": "INT UNSIGNED",
    "int64_t": "BIGINT",
    "uint64_t": "BIGINT UNSIGNED",
    "float": "FLOAT",
    "double": "DOUBLE",
    "std::string": "VARCHAR",
}

# C++ 成员的默认初始化字面量；std::string 依赖默认构造，不写初始化器
_DEFAULT_LITERAL = {
    "bool": "false",
    "int8_t": "0",
    "uint8_t": "0",
    "int16_t": "0",
    "uint16_t": "0",
    "int32_t": "0",
    "uint32_t": "0",
    "int64_t": "0",
    "uint64_t": "0",
    "float": "0.0f",
    "double": "0.0",
}


def supported_types():
    """返回生成器支持的全部 C++ 类型名（已排序）。"""
    return sorted(_MYSQL_TYPE_MAP.keys())


def is_supported_type(cpp_type):
    return cpp_type in _MYSQL_TYPE_MAP


def is_string_type(cpp_type):
    return cpp_type == "std::string"


def mysql_type(cpp_type, length=None):
    """返回该 C++ 类型对应的 MySQL 列类型。

    :param length: 仅对 std::string 生效，覆盖默认 VARCHAR 宽度
    """
    if not is_supported_type(cpp_type):
        raise ValueError("不支持的 C++ 类型: %s（支持的: %s）" % (cpp_type, ", ".join(supported_types())))

    if not is_string_type(cpp_type):
        return _MYSQL_TYPE_MAP[cpp_type]

    return "VARCHAR(%d)" % (length if length else DEFAULT_VARCHAR_LEN)


def default_literal(cpp_type):
    """返回 C++ 成员的默认初始化字面量，std::string 返回 None。"""
    if not is_supported_type(cpp_type):
        raise ValueError("不支持的 C++ 类型: %s" % cpp_type)
    return _DEFAULT_LITERAL.get(cpp_type)


def from_row_expr(cpp_type, field_name):
    """返回 from_row() 中把某一列反序列化为 C++ 值的表达式。

    统一走 rowText() 再 cast_to<>()：
      - rowText() 兜住 NULL 列 c_str() 可能返回空指针的问题；
      - cast_to 的双参数版本转换失败时返回默认值而不是抛异常，
        避免 boost::bad_lexical_cast 穿透 loadWhere 的 catch 块。
    """
    if not is_supported_type(cpp_type):
        raise ValueError("不支持的 C++ 类型: %s" % cpp_type)

    text = 'rowText(row, "%s")' % field_name
    if is_string_type(cpp_type):
        return text

    return "cast_to<%s>(%s, %s)" % (cpp_type, text, default_literal(cpp_type))


# =============================================================================
# 2. 表定义解析：src/database/dbtables/tables.xml 的解析与校验
#
#     <tables>
#         <table name="Stock" keys="item_id" comment="">
#             <column index="1" field="item_id" type="std::string" comment="物品id" />
#         </table>
#     </tables>
#
# - keys 支持逗号分隔的复合主键；
# - column 的 len 属性可选，仅对 std::string 生效（覆盖默认 VARCHAR 宽度）。
# =============================================================================


class SchemaError(Exception):
    """tables.xml 结构或内容不合法。"""


@dataclass
class Column:
    index: int
    field: str
    cpp_type: str
    comment: str = ""
    length: Optional[int] = None


@dataclass
class Table:
    name: str
    comment: str
    columns: List[Column]
    keys: List[str]

    def column(self, field_name):
        for col in self.columns:
            if col.field == field_name:
                return col
        raise SchemaError("表 %s 中不存在字段 %s" % (self.name, field_name))

    def key_columns(self):
        return [self.column(key) for key in self.keys]


def _parse_int(value, what):
    try:
        return int(value)
    except (TypeError, ValueError):
        raise SchemaError("%s 不是合法整数: %r" % (what, value))


def _parse_table(elem):
    name = elem.get("name")
    if not name:
        raise SchemaError("<table> 缺少 name 属性")

    keys = [key.strip() for key in (elem.get("keys") or "").split(",") if key.strip()]

    columns = []
    for col_elem in elem.findall("column"):
        field_name = col_elem.get("field")
        cpp_type = col_elem.get("type")
        if not field_name:
            raise SchemaError("表 %s 的 <column> 缺少 field 属性" % name)
        if not cpp_type:
            raise SchemaError("表 %s 的列 %s 缺少 type 属性" % (name, field_name))
        if not is_supported_type(cpp_type):
            raise SchemaError("表 %s 的列 %s 使用了生成器不支持的 C++ 类型: %s" % (name, field_name, cpp_type))

        index = _parse_int(col_elem.get("index"), "表 %s 的列 %s 的 index" % (name, field_name))

        length = col_elem.get("len")
        columns.append(
            Column(
                index=index,
                field=field_name,
                cpp_type=cpp_type,
                comment=col_elem.get("comment") or "",
                length=_parse_int(length, "表 %s 的列 %s 的 len" % (name, field_name)) if length else None,
            )
        )

    if not columns:
        raise SchemaError("表 %s 没有任何 <column>" % name)

    # 按 index 排序，保证生成顺序稳定且与 XML 书写顺序无关
    columns.sort(key=lambda col: col.index)

    indexes = [col.index for col in columns]
    if len(set(indexes)) != len(indexes):
        raise SchemaError("表 %s 的 column index 存在重复: %s" % (name, indexes))
    if indexes != list(range(1, len(columns) + 1)):
        raise SchemaError("表 %s 的 column index 必须从 1 连续递增，实际为 %s" % (name, indexes))

    fields = [col.field for col in columns]
    if len(set(fields)) != len(fields):
        raise SchemaError("表 %s 存在重名字段: %s" % (name, fields))

    for key in keys:
        if key not in fields:
            raise SchemaError("表 %s 的 keys 引用了不存在的字段: %s" % (name, key))

    return Table(name=name, comment=elem.get("comment") or "", columns=columns, keys=keys)


def parse_tables(xml_path):
    """解析 tables.xml，返回 Table 列表。任何不合法之处抛 SchemaError。"""
    root = ET.parse(xml_path).getroot()

    table_elems = root.findall("table")
    if not table_elems:
        raise SchemaError("%s 中没有找到任何 <table>" % xml_path)

    tables = [_parse_table(elem) for elem in table_elems]

    names = [table.name for table in tables]
    if len(set(names)) != len(names):
        raise SchemaError("存在重名表: %s" % names)

    return tables


# =============================================================================
# 3. DDL 渲染
#
# 约定（见 ADR-001-db-codegen.md）：
#   - ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
#   - 所有列 NOT NULL（XML 不携带 nullable 信息）
#   - 不生成 DEFAULT（XML 不携带默认值，不替业务编语义）
#   - 列注释 / 表注释取自 XML 的 comment，为空则省略
#   - 不生成 DROP TABLE（破坏性语句不进版本库）
# =============================================================================

_HEADER = (
    "-- ---------------------------------------------------------------------------\n"
    "-- 本文件由 tinytools/xml2db.py 从 src/database/dbtables/tables.xml 自动生成\n"
    "-- 请勿手工修改，改表请改 XML 后重新运行生成器\n"
    "-- 生成时间: %s\n"
    "-- ---------------------------------------------------------------------------\n"
)


def _escape_comment(text):
    return text.replace("\\", "\\\\").replace("'", "''")


def _quote_identifier(name):
    return "`%s`" % name


def render_table_ddl(table):
    """渲染单张表的 CREATE TABLE 语句。"""
    name_width = max(len(_quote_identifier(col.field)) for col in table.columns)
    type_width = max(len(mysql_type(col.cpp_type, col.length)) for col in table.columns)

    lines = []
    for col in table.columns:
        column = "    %s %s NOT NULL" % (
            _quote_identifier(col.field).ljust(name_width),
            mysql_type(col.cpp_type, col.length).ljust(type_width),
        )
        if col.comment:
            column += " COMMENT '%s'" % _escape_comment(col.comment)
        lines.append(column)

    key_list = ", ".join(_quote_identifier(key) for key in table.keys)
    if key_list:
        lines.append("    PRIMARY KEY (%s)" % key_list)

    tail = ") ENGINE=%s DEFAULT CHARSET=%s" % (DEFAULT_ENGINE, DEFAULT_CHARSET)
    if table.comment:
        tail += " COMMENT='%s'" % _escape_comment(table.comment)

    return "CREATE TABLE IF NOT EXISTS %s (\n%s\n%s;\n" % (
        _quote_identifier(table.name),
        ",\n".join(lines),
        tail,
    )


def render_sql(tables):
    """渲染整份 SQL 文件内容（包含文件头注释）。"""
    parts = [_HEADER % datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")]
    parts.extend(render_table_ddl(table) for table in tables)
    return "\n".join(parts)


# =============================================================================
# 4. C++ 渲染：<table>.h（声明）+ <table>.cpp（实现）
#
# 遵循 CODE_STYLE.md §4.1：头文件只放声明、源文件放实现。
# 行类型与该表的数据访问接口是同一个类（见 ADR-002）：数据访问接口全部是静态函数，
# 连接在函数内部用 ScopedMySqlConn 从连接池取，因此头文件既不持有 MysqlConn&，也
# 不需要 mysql_conn.h（及其 config.h / connection_pool.h / logger.h 依赖）——
# 该依赖只落在 <table>.cpp 里，行类型的使用者不承担。
#
# 生成的行类型满足 MysqlConn 的 ORM 契约（见 src/database/dbtables/CONTEXT.md）：
#     static const char* table()
#     static const char* field_list()
#     void value_list(mysqlpp::Query&) const
#     static T from_row(const mysqlpp::Row&)
# =============================================================================


def to_snake_case(name):
    """Stock -> stock；PlayerBag -> player_bag。"""
    out = []
    for i, ch in enumerate(name):
        if ch.isupper() and i > 0 and not name[i - 1].isupper():
            out.append("_")
        out.append(ch.lower())
    return "".join(out)


def header_file_name(table):
    return "%s.h" % to_snake_case(table.name)


def source_file_name(table):
    return "%s.cpp" % to_snake_case(table.name)


def _file_comment(file_name, brief):
    return (
        "/**\n"
        " * @file %s\n"
        " * @brief %s\n"
        " * @author xml2db（由 tinytools/xml2db.py 从 src/database/dbtables/tables.xml 生成，请勿手工修改）\n"
        " * @date %s\n"
        " */\n" % (file_name, brief, datetime.date.today().isoformat())
    )


def _namespace_parts(namespace):
    """cncpp::db -> ['cncpp', 'db']。"""
    return [part.strip() for part in namespace.split("::") if part.strip()]


def _open_namespaces(namespace, preamble=None):
    """打开嵌套命名空间；preamble 的每一项插在最外层 { 之后、内层命名空间之前。"""
    parts = _namespace_parts(namespace)
    lines = ["namespace %s\n{\n" % parts[0]]
    if preamble:
        lines.extend(preamble)
    for i, part in enumerate(parts[1:], start=1):
        lines.append("%snamespace %s\n%s{\n" % (" " * (4 * i), part, " " * (4 * i)))
    return lines


def _close_namespaces(namespace, level=0):
    parts = _namespace_parts(namespace)
    total = len(parts)
    return [
        "%s}  // namespace %s\n" % (" " * (4 * (level + total - 1 - i)), part)
        for i, part in enumerate(reversed(parts))
    ]


# 行宽上限与续行缩进（CODE_STYLE.md §2.1 / §2.4）
MAX_LINE_WIDTH = 120
LINE_CONTINUATION = "    "


def _join_lines(prefix, items, suffix, indent, separator=", "):
    """拼接 indent + prefix + items + suffix；超过 MAX_LINE_WIDTH 时每项独占一行。

    用于构造函数的形参表与成员初始化列表：列数与列名长度都由表定义决定，
    无法预先保证拼出来的一行不越界。
    """
    single = "%s%s%s%s\n" % (indent, prefix, separator.join(items), suffix)
    if len(single.rstrip("\n")) <= MAX_LINE_WIDTH:
        return [single]

    lines = []
    for i, item in enumerate(items):
        head = prefix if i == 0 else ""
        pad = indent if i == 0 else indent + LINE_CONTINUATION
        tail = separator.rstrip() if i < len(items) - 1 else suffix
        lines.append("%s%s%s%s\n" % (pad, head, item, tail))
    return lines


def _param(col):
    """形参：字符串走 const&（CODE_STYLE.md §2.5），数值按值传递。"""
    if is_string_type(col.cpp_type):
        return "const %s& %s" % (col.cpp_type, col.field)
    return "%s %s" % (col.cpp_type, col.field)


def _param_list(columns):
    """逗号分隔的形参表。"""
    return ", ".join(_param(col) for col in columns)


def _getter_name(field):
    """item_id -> getItemId（CODE_STYLE.md §1.1 驼峰、首字母小写）。"""
    parts = [part for part in field.split("_") if part]
    return "get" + "".join(part[:1].upper() + part[1:] for part in parts)


def _getter_return_type(cpp_type):
    """字符串按 const& 返回（避免拷贝大对象），数值按值返回。"""
    return "const %s&" % cpp_type if is_string_type(cpp_type) else cpp_type


def _member_declarations(table, level):
    pad = " " * (4 * level)
    type_width = max(len(col.cpp_type) for col in table.columns)
    lines = []
    for col in table.columns:
        literal = default_literal(col.cpp_type)
        initializer = " = %s;" % literal if literal else ";"
        lines.append("%s%s %s%s\n" % (pad, col.cpp_type.ljust(type_width), col.field, initializer))
    return lines


def _value_list_body(table, level):
    pad = " " * (4 * level)
    lines = []
    for i, col in enumerate(table.columns):
        if i > 0:
            lines.append('%squery << ",";\n' % pad)
        lines.append("%ssqlValue(query, %s);\n" % (pad, col.field))
    return lines


def _from_row_body(table, level):
    pad = " " * (4 * level)
    lines = ["%s%s item;\n" % (pad, table.name)]
    for col in table.columns:
        lines.append("%sitem.%s = %s;\n" % (pad, col.field, from_row_expr(col.cpp_type, col.field)))
    lines.append("%sreturn item;\n" % pad)
    return lines


def _cpp_string_literal(text):
    """C++ 字符串字面量：转义反斜杠与双引号。"""
    return '"%s"' % text.replace("\\", "\\\\").replace('"', '\\"')


def _meta_body(table, depth):
    """渲染 <table>::meta()：列数组与表 Meta 都做成函数内 static，首次调用时构造。

    Meta 与 DDL 出自同一份表定义、共用 DEFAULT_ENGINE / DEFAULT_CHARSET，因此不可能
    与建表语句漂移。

    :param depth: 命名空间层数，决定缩进
    """
    pad = " " * (4 * depth)
    fn_pad = " " * (4 * (depth + 1))
    row_pad = " " * (4 * (depth + 2))

    name_width = max(len(_cpp_string_literal(col.field)) for col in table.columns)
    type_width = max(len(_cpp_string_literal(mysql_type(col.cpp_type, col.length))) for col in table.columns)

    # tables.xml 不携带 nullable 信息，DDL 恒为 NOT NULL（见 ADR-001）
    not_null = "true"

    lines = ["%sconst TableMeta& %s::meta()\n%s{\n" % (pad, table.name, pad)]
    lines.append("%sstatic const ColumnMeta k_columns[] = {\n" % fn_pad)
    for col in table.columns:
        lines.append("%s{ %s, %s, %s, %s, %s },\n"
                     % (row_pad,
                        _cpp_string_literal(col.field).ljust(name_width),
                        _cpp_string_literal(mysql_type(col.cpp_type, col.length)).ljust(type_width),
                        _cpp_string_literal(col.comment),
                        not_null,
                        "true" if col.field in table.keys else "false"))
    lines.append("%s};\n\n" % fn_pad)

    lines.append("%sstatic const TableMeta k_meta = {\n" % fn_pad)
    lines.append('%s%s, "%s", "%s", %s, k_columns, %d\n'
                 % (row_pad, _cpp_string_literal(table.name),
                    DEFAULT_ENGINE, DEFAULT_CHARSET,
                    _cpp_string_literal(table.comment), len(table.columns)))
    lines.append("%s};\n\n" % fn_pad)
    lines.append("%sreturn k_meta;\n" % fn_pad)
    lines.append("%s}\n\n" % pad)
    return lines


def render_header(table, namespace):
    """渲染 <table>.h：行类型与数据访问接口的声明。返回 (文件名, 内容)。"""
    file_name = header_file_name(table)
    key_params = _key_parameters(table)

    depth = len(_namespace_parts(namespace))
    body_pad = " " * (4 * depth)
    fn_pad = " " * (4 * (depth + 1))

    parts = [_file_comment(file_name, "表 %s 的行类型与数据访问接口" % table.name), "\n"]
    parts.append("#pragma once\n\n")
    parts.append("#include <cstdint>\n")
    parts.append("#include <optional>\n")
    parts.append("#include <string>\n")
    parts.append("#include <vector>\n\n")
    parts.append("#include <mysql++.h>\n\n")
    parts.append('#include "table_meta.h"\n\n')

    parts.extend(_open_namespaces(namespace))
    parts.append("\n")

    parts.append("%s/**\n" % body_pad)
    parts.append("%s * @brief 表 %s 的一行数据，并承载该表的数据访问接口\n" % (body_pad, table.name))
    parts.append("%s *\n" % body_pad)
    parts.append("%s * 成员按「构造函数 / getter / 成员函数 / 静态函数 / 数据成员」分区，便于按类别定位。\n" % body_pad)
    parts.append("%s *\n" % body_pad)
    parts.append("%s * 数据成员私有、只经 getter 读出：对象一经构造即只读，整行写入靠重新构造而非逐字段修改。\n" % body_pad)
    parts.append("%s *\n" % body_pad)
    parts.append("%s * ORM 契约：table() / field_list() / value_list(Query&) / from_row(Row)。\n" % body_pad)
    parts.append("%s *\n" % body_pad)
    parts.append("%s * Meta：meta() 给出本表的结构（列名 / MySQL 类型 / NOT NULL / 主键），\n" % body_pad)
    parts.append("%s * 建表语句与字段列表都由它派生，因此列名只在生成物里存一份。\n" % body_pad)
    parts.append("%s *\n" % body_pad)
    parts.append("%s * 数据访问接口全部是静态函数：连接由函数内部从连接池取（ScopedMySqlConn），\n" % body_pad)
    parts.append("%s * 调用方无需持有、也无需传入 MysqlConn。\n" % body_pad)
    parts.append("%s *\n" % body_pad)
    parts.append("%s * WHERE 条件的拼接与转义全部收在 %s 里，裸 where 字符串不对外暴露。\n"
                 % (body_pad, source_file_name(table)))
    parts.append("%s */\n" % body_pad)
    parts.append("%sstruct %s\n%s{\n" % (body_pad, table.name, body_pad))
    parts.append("%spublic:\n" % body_pad)

    parts.append("%s// ---------- 构造函数 ----------\n" % fn_pad)
    parts.append("%s/** @brief 默认构造：各字段取类型零值，主要供 from_row() 与容器使用 */\n" % fn_pad)
    parts.append("%s%s() = default;\n\n" % (fn_pad, table.name))
    parts.append("%s/** @brief 逐字段构造 */\n" % fn_pad)
    parts.extend(_join_lines("%s(" % table.name,
                             [_param(col) for col in table.columns], ");", fn_pad))
    parts.append("\n")

    parts.append("%s// ---------- getter ----------\n" % fn_pad)
    for col in table.columns:
        if col.comment:
            parts.append("%s/** @brief %s */\n" % (fn_pad, col.comment))
        parts.append("%s%s %s() const;\n\n"
                     % (fn_pad, _getter_return_type(col.cpp_type), _getter_name(col.field)))

    parts.append("%s// ---------- 成员函数 ----------\n" % fn_pad)
    parts.append("%s/**\n" % fn_pad)
    parts.append("%s * @brief 把整行的值写进查询流，供 REPLACE INTO ... VALUES (...) 使用\n" % fn_pad)
    parts.append("%s * @param query 已绑定连接的 mysql++ 查询流，字符串需要靠它做转义\n" % fn_pad)
    parts.append("%s */\n" % fn_pad)
    parts.append("%svoid value_list(mysqlpp::Query& query) const;\n" % fn_pad)
    parts.append("\n")

    parts.append("%s// ---------- 静态函数 ----------\n" % fn_pad)
    parts.append("%s/** @brief 本表的 Meta：建表语句与字段列表都由它派生（见 ADR-005） */\n" % fn_pad)
    parts.append("%sstatic const TableMeta& meta();\n\n" % fn_pad)

    parts.append("%s/** @brief 表名 */\n" % fn_pad)
    parts.append("%sstatic const char* table();\n\n" % fn_pad)

    parts.append("%s/** @brief SELECT / REPLACE 共用的字段列表 */\n" % fn_pad)
    parts.append("%sstatic const char* field_list();\n\n" % fn_pad)

    parts.append("%s/** @brief 从结果集的一行构造对象 */\n" % fn_pad)
    parts.append("%sstatic %s from_row(const mysqlpp::Row& row);\n\n" % (fn_pad, table.name))

    parts.append("%s/** @brief 加载全表（无 LIMIT，仅适用于配置量级的表） */\n" % fn_pad)
    parts.append("%sstatic std::vector<%s> loadAll();\n\n" % (fn_pad, table.name))

    parts.append("%s/** @brief 按主键加载单行，未命中返回 std::nullopt */\n" % fn_pad)
    parts.append("%sstatic std::optional<%s> loadByKey(%s);\n\n" % (fn_pad, table.name, key_params))

    parts.append("%s/** @brief 整行写入（主键已存在则整行覆盖） */\n" % fn_pad)
    parts.append("%sstatic uint64_t save(const %s& row);\n\n" % (fn_pad, table.name))

    parts.append("%s/** @brief 批量整行写入，返回受影响行数 */\n" % fn_pad)
    parts.append("%sstatic uint64_t saveAll(const std::vector<%s>& rows);\n\n" % (fn_pad, table.name))

    parts.append("%s/** @brief 按主键删除 */\n" % fn_pad)
    parts.append("%sstatic uint64_t deleteByKey(%s);\n\n" % (fn_pad, key_params))

    parts.append("%s/**\n" % fn_pad)
    parts.append("%s * @brief 逃生口：复杂条件查询\n" % fn_pad)
    parts.append("%s * @param where 直接拼进 WHERE 子句的条件，调用方自行保证其合法性\n" % fn_pad)
    parts.append("%s */\n" % fn_pad)
    parts.append("%sstatic std::vector<%s> loadWhere(const std::string& where);\n\n" % (fn_pad, table.name))

    parts.append("%sprivate:\n" % body_pad)
    parts.append("%s// ---------- 数据成员 ----------\n" % fn_pad)
    parts.extend(_member_declarations(table, depth + 1))
    parts.append("%s};\n" % body_pad)

    parts.extend(_close_namespaces(namespace))

    return file_name, "".join(parts)


def _key_parameters(table):
    """主键参数列表。"""
    return _param_list(table.key_columns())


def _where_body(table, level):
    """渲染 where 条件：主键等值，字符串经 sqlValue 转义。"""
    pad = " " * (4 * level)
    lines = ["%smysqlpp::Query where = con->getConn().query();\n" % pad]
    for i, col in enumerate(table.key_columns()):
        prefix = "`" if i == 0 else " AND `"
        lines.append('%swhere << "%s%s` = ";\n' % (pad, prefix, col.field))
        lines.append("%ssqlValue(where, %s);\n" % (pad, col.field))
    return lines


def render_source(table, namespace):
    """渲染 <table>.cpp：行类型契约与数据访问接口的实现。返回 (文件名, 内容)。"""
    file_name = source_file_name(table)
    key_params = _key_parameters(table)

    depth = len(_namespace_parts(namespace))
    body_pad = " " * (4 * depth)
    fn_pad = " " * (4 * (depth + 1))

    parts = [_file_comment(file_name, "表 %s 的行类型契约与数据访问接口的实现" % table.name), "\n"]
    parts.append('#include "%s"\n\n' % header_file_name(table))
    parts.append('#include "mysql_conn.h"\n')
    parts.append('#include "sql_value.h"\n')
    parts.append('#include "utils.h"\n\n')

    parts.extend(_open_namespaces(namespace))
    parts.append("\n")

    parts.append("%s// ---------- 构造函数 ----------\n" % body_pad)
    parts.extend(_join_lines("%s::%s(" % (table.name, table.name),
                             [_param(col) for col in table.columns], ")", body_pad))
    parts.extend(_join_lines(": ",
                             ["%s(%s)" % (col.field, col.field) for col in table.columns], "", fn_pad))
    parts.append("%s{\n%s}\n\n" % (body_pad, body_pad))

    parts.append("%s// ---------- getter ----------\n" % body_pad)
    for col in table.columns:
        parts.append("%s%s %s::%s() const\n%s{\n"
                     % (body_pad, _getter_return_type(col.cpp_type), table.name, _getter_name(col.field), body_pad))
        parts.append("%sreturn %s;\n" % (fn_pad, col.field))
        parts.append("%s}\n\n" % body_pad)

    parts.append("%s// ---------- 成员函数 ----------\n" % body_pad)
    parts.append("%svoid %s::value_list(mysqlpp::Query& query) const\n%s{\n" % (body_pad, table.name, body_pad))
    parts.extend(_value_list_body(table, depth + 1))
    parts.append("%s}\n\n" % body_pad)

    parts.append("%s// ---------- 静态函数 ----------\n" % body_pad)
    parts.extend(_meta_body(table, depth))

    parts.append("%sconst char* %s::table()\n%s{\n" % (body_pad, table.name, body_pad))
    parts.append("%sreturn meta().name;\n" % fn_pad)
    parts.append("%s}\n\n" % body_pad)

    parts.append("%sconst char* %s::field_list()\n%s{\n" % (body_pad, table.name, body_pad))
    parts.append("%s// 由 Meta 派生；函数内 static 保证只拼一次，首次调用的初始化是线程安全的\n" % fn_pad)
    parts.append("%sstatic const std::string fields = fieldListSql(meta());\n" % fn_pad)
    parts.append("%sreturn fields.c_str();\n" % fn_pad)
    parts.append("%s}\n\n" % body_pad)

    parts.append("%s%s %s::from_row(const mysqlpp::Row& row)\n%s{\n" % (body_pad, table.name, table.name, body_pad))
    parts.extend(_from_row_body(table, depth + 1))
    parts.append("%s}\n\n" % body_pad)

    parts.append("%sstd::vector<%s> %s::loadAll()\n%s{\n" % (body_pad, table.name, table.name, body_pad))
    parts.append("%sScopedMySqlConn con;\n" % fn_pad)
    parts.append('%sreturn con->loadWhere<%s>("");\n' % (fn_pad, table.name))
    parts.append("%s}\n\n" % body_pad)

    parts.append("%sstd::optional<%s> %s::loadByKey(%s)\n%s{\n" % (body_pad, table.name, table.name, key_params, body_pad))
    parts.append("%sScopedMySqlConn con;\n" % fn_pad)
    parts.extend(_where_body(table, depth + 1))
    parts.append("\n")
    parts.append("%sstd::vector<%s> rows = con->loadWhere<%s>(where.str());\n" % (fn_pad, table.name, table.name))
    parts.append("%sif (rows.empty())\n%s{\n" % (fn_pad, fn_pad))
    parts.append("%s%sreturn std::nullopt;\n" % (fn_pad, " " * 4))
    parts.append("%s}\n" % fn_pad)
    parts.append("%sreturn rows.front();\n" % fn_pad)
    parts.append("%s}\n\n" % body_pad)

    parts.append("%suint64_t %s::save(const %s& row)\n%s{\n" % (body_pad, table.name, table.name, body_pad))
    parts.append("%sScopedMySqlConn con;\n" % fn_pad)
    parts.append("%sreturn con->replaceDB(row);\n" % fn_pad)
    parts.append("%s}\n\n" % body_pad)

    parts.append("%suint64_t %s::saveAll(const std::vector<%s>& rows)\n%s{\n" % (body_pad, table.name, table.name, body_pad))
    parts.append("%sScopedMySqlConn con;\n" % fn_pad)
    parts.append("%sreturn con->replaceAll<%s>(rows);\n" % (fn_pad, table.name))
    parts.append("%s}\n\n" % body_pad)

    parts.append("%suint64_t %s::deleteByKey(%s)\n%s{\n" % (body_pad, table.name, key_params, body_pad))
    parts.append("%sScopedMySqlConn con;\n" % fn_pad)
    parts.extend(_where_body(table, depth + 1))
    parts.append("\n")
    parts.append("%sreturn con->deleteWhere<%s>(where.str());\n" % (fn_pad, table.name))
    parts.append("%s}\n\n" % body_pad)

    parts.append("%sstd::vector<%s> %s::loadWhere(const std::string& where)\n%s{\n" % (body_pad, table.name, table.name, body_pad))
    parts.append("%sScopedMySqlConn con;\n" % fn_pad)
    parts.append("%sreturn con->loadWhere<%s>(where);\n" % (fn_pad, table.name))
    parts.append("%s}\n" % body_pad)

    parts.extend(_close_namespaces(namespace))

    return file_name, "".join(parts)


# =============================================================================
# 4.5 Meta 清单：all_table_metas.h + all_table_metas.cpp
#
# 每张表的 Meta 挂在它自己的行类型上（<table>::meta()），但「全部表」这份清单必须显式
# 枚举：生成物打成静态库 db，静态库中未被直接引用的 translation unit 不参与链接，靠
# static initializer 自注册会静默丢表（见 ADR-005 D5）。
#
# 清单只列表名，不复述任何列信息 —— 列信息已经在那张表自己的 meta() 里了。
# =============================================================================

ALL_METAS_HEADER = "all_table_metas.h"
ALL_METAS_SOURCE = "all_table_metas.cpp"


def render_all_metas_header(tables, namespace):
    """渲染 all_table_metas.h：全部表 Meta 的清单声明。返回 (文件名, 内容)。"""
    file_name = ALL_METAS_HEADER

    depth = len(_namespace_parts(namespace))
    body_pad = " " * (4 * depth)

    parts = [_file_comment(file_name, "全部表 Meta 的清单"), "\n"]
    parts.append("#pragma once\n\n")
    parts.append("#include <vector>\n\n")
    parts.append('#include "table_meta.h"\n\n')

    parts.extend(_open_namespaces(namespace))
    parts.append("\n")

    parts.append("%s/**\n" % body_pad)
    parts.append("%s * @brief 全部表的 Meta，顺序与 tables.xml 一致\n" % body_pad)
    parts.append("%s *\n" % body_pad)
    parts.append("%s * 由生成器显式枚举（见 ADR-005 D5）：不用自注册，避免静态库链接时被静默丢弃。\n" % body_pad)
    parts.append("%s */\n" % body_pad)
    parts.append("%sconst std::vector<const TableMeta*>& allTableMetas();\n" % body_pad)

    parts.extend(_close_namespaces(namespace))

    return file_name, "".join(parts)


def render_all_metas_source(tables, namespace):
    """渲染 all_table_metas.cpp：全部表 Meta 的清单定义。返回 (文件名, 内容)。"""
    file_name = ALL_METAS_SOURCE

    depth = len(_namespace_parts(namespace))
    body_pad = " " * (4 * depth)
    fn_pad = " " * (4 * (depth + 1))

    parts = [_file_comment(file_name, "全部表 Meta 的清单定义"), "\n"]
    parts.append('#include "%s"\n\n' % ALL_METAS_HEADER)
    for table in tables:
        parts.append('#include "%s"\n' % header_file_name(table))
    parts.append("\n")

    parts.extend(_open_namespaces(namespace))
    parts.append("\n")

    parts.append("%sconst std::vector<const TableMeta*>& allTableMetas()\n%s{\n" % (body_pad, body_pad))
    parts.append("%sstatic const std::vector<const TableMeta*> k_all = {\n" % fn_pad)
    for table in tables:
        parts.append("%s&%s::meta(),\n" % (fn_pad + " " * 4, table.name))
    parts.append("%s};\n" % fn_pad)
    parts.append("%sreturn k_all;\n" % fn_pad)
    parts.append("%s}\n" % body_pad)

    parts.extend(_close_namespaces(namespace))

    return file_name, "".join(parts)


def _render_all_metas_files(tables, namespace):
    """返回 Meta 清单需要写出的 (文件名, 内容) 列表。"""
    return [render_all_metas_header(tables, namespace),
            render_all_metas_source(tables, namespace)]


# =============================================================================
# 5. CLI 入口
# =============================================================================

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
# tinytools/ 位于仓库根下一级
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, os.pardir))

DEFAULT_XML = os.path.join("src", "database", "dbtables", "tables.xml")
DEFAULT_SQL_OUT = os.path.join("src", "database", "sql")
DEFAULT_CPP_OUT = os.path.join("src", "database", "dbtables")
DEFAULT_NAMESPACE = "cncpp::db"


def _resolve(path):
    return path if os.path.isabs(path) else os.path.join(REPO_ROOT, path)


def _manifest_path(path):
    """清单里的路径统一用正斜杠：CMake 在 Windows 上不认反斜杠。"""
    return os.path.abspath(path).replace("\\", "/")


def _write_file(path, content):
    """写出文件，返回是否真的发生了写入。

    内容与磁盘一致时直接跳过：构建系统会在 configure 阶段调用本脚本，若无条件重写，
    每次配置都会刷新 mtime，下游据此把全部生成物重编一遍。
    """
    if os.path.isfile(path):
        try:
            with open(path, "r", encoding="utf-8", newline="") as handle:
                if handle.read() == content:
                    return False
        except OSError:
            pass

    directory = os.path.dirname(path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(content)
    return True


def _render_table_files(table, namespace):
    """返回该表需要写出的 (文件名, 内容) 列表。"""
    return [render_header(table, namespace), render_source(table, namespace)]


def main(argv=None):
    parser = argparse.ArgumentParser(description="从 tables.xml 生成 MySQL DDL 与 C++ 访问层")
    parser.add_argument("--xml", default=DEFAULT_XML, help="输入的 tables.xml（默认 src/database/dbtables/tables.xml）")
    parser.add_argument("--sql-out", default=DEFAULT_SQL_OUT, help="DDL 输出目录（默认 src/database/sql）")
    parser.add_argument("--cpp-out", default=DEFAULT_CPP_OUT, help="C++ 源码输出目录（默认 src/database/dbtables）")
    parser.add_argument("--namespace", default=DEFAULT_NAMESPACE, help="生成的 C++ 命名空间（默认 cncpp::db）")
    parser.add_argument(
        "--sql-per-table",
        action="store_true",
        help="每张表单独输出一个 .sql 文件（默认全部表合并到 tables.sql）",
    )
    parser.add_argument(
        "--manifest",
        default=None,
        help="把本次生成的全部文件按行写入该文件，供构建系统取得精确的生成物清单",
    )
    args = parser.parse_args(argv)

    xml_path = _resolve(args.xml)
    if not os.path.isfile(xml_path):
        print("[xml2db] 找不到输入文件: %s" % xml_path, file=sys.stderr)
        return 1

    try:
        tables = parse_tables(xml_path)
    except SchemaError as err:
        print("[xml2db] tables.xml 校验失败: %s" % err, file=sys.stderr)
        return 1

    sql_out = _resolve(args.sql_out)
    cpp_out = _resolve(args.cpp_out)

    # 先收集再统一写出：清单必须与实际写出的文件严格一致
    outputs = []
    if args.sql_per_table:
        for table in tables:
            outputs.append((os.path.join(sql_out, "%s.sql" % to_snake_case(table.name)),
                            render_sql([table])))
    else:
        outputs.append((os.path.join(sql_out, "tables.sql"), render_sql(tables)))

    for table in tables:
        outputs.extend((os.path.join(cpp_out, file_name), content)
                       for file_name, content in _render_table_files(table, args.namespace))

    # Meta 清单不属于任何一张表，是整份表定义的投影，单独产出一份
    outputs.extend((os.path.join(cpp_out, file_name), content)
                   for file_name, content in _render_all_metas_files(tables, args.namespace))

    changed = 0
    for target, content in outputs:
        if _write_file(target, content):
            changed += 1
            print("[xml2db] 生成 %s" % os.path.relpath(target, REPO_ROOT))

    if changed < len(outputs):
        print("[xml2db] %d 个文件内容未变化，已跳过写入" % (len(outputs) - changed))

    if args.manifest:
        manifest_path = _resolve(args.manifest)
        _write_file(manifest_path,
                    "".join("%s\n" % _manifest_path(target) for target, _ in outputs))
        print("[xml2db] 生成 %s" % os.path.relpath(manifest_path, REPO_ROOT))

    print("[xml2db] 完成，共 %d 张表" % len(tables))
    return 0


if __name__ == "__main__":
    sys.exit(main())

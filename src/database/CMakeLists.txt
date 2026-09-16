# ============================================
# 数据库访问层（dbtables/<table>.{h,cpp} 与 sql/<table>.sql 由 tinytools/xml2db.py 生成）
#
# 生成物（勿手工修改，改 dbtables/tables.xml 后重新构建即可）：
#   dbtables/<table>.h    行类型 + 数据访问接口 + Meta 的声明
#   dbtables/<table>.cpp  行类型契约与数据访问接口 + Meta 的实现（CODE_STYLE.md §4.1 声明与实现分离）
#   dbtables/all_table_metas.{h,cpp}  全部表 Meta 的清单，只列表名（见 ADR-005 D5）
#   sql/<table>.sql       CREATE TABLE DDL，不参与构建（DDL 已改为运行时由 Meta 派生）
# 手写文件（不生成、也不清理）：dbtables/sql_value.h、dbtables/table_meta.h、dbtables/tables.xml、
#                              本目录下的 *.md
#
# 生成闭环：
#   configure  先跑一次生成器，产物就位后下面的 GLOB 才能收全（首次配置、clean 之后都需要）。
#              .h / .cpp 内容不变时生成器跳过写入，不会刷新 mtime 拖累增量编译；
#              sql/<table>.sql 的头部带生成时间戳，每次配置都会重写一次 —— 它不参与编译，
#              且总是新于 tables.xml，因此不会引起重生成
#   build      tables.xml 或生成器变化时由 add_custom_command 重跑
#   clean      ADDITIONAL_CLEAN_FILES 按生成器给出的清单精确删除，不碰手写文件。
#              该属性不被 Visual Studio 生成器支持（Linux/Makefile、Ninja 下有效）
#
# 清单由生成器自己输出（--manifest），不用 GLOB 猜：表名到文件名的 snake_case 转换只有
# 生成器知道（StockItem -> stock_item），且 dbtables/ 里混着手写的 sql_value.h。
# 新增表后需重跑一次 cmake：GLOB 在 configure 时求值，不会因新文件自动刷新。
# ============================================

set(DB_TABLES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dbtables")
set(DB_SQL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sql")
set(DB_TABLES_XML "${DB_TABLES_DIR}/tables.xml")
set(DB_GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/../../tinytools/xml2db.py")
set(DB_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/db_generated_files.txt")

# 不用 FindPython3：它需要 CMake 3.12，而本工程声明的是 cmake_minimum_required(3.10)
find_program(DB_PYTHON_EXECUTABLE NAMES python3 python)
if(NOT DB_PYTHON_EXECUTABLE)
    message(FATAL_ERROR "找不到 python3/python，无法运行 ${DB_GENERATOR}")
endif()

set(DB_GENERATOR_ARGS "${DB_GENERATOR}" --sql-per-table --manifest "${DB_MANIFEST}")

execute_process(
    COMMAND "${DB_PYTHON_EXECUTABLE}" ${DB_GENERATOR_ARGS}
    RESULT_VARIABLE DB_GEN_RESULT
)
if(NOT DB_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "xml2db.py 执行失败（exit=${DB_GEN_RESULT}），请检查 ${DB_TABLES_XML}")
endif()

file(STRINGS "${DB_MANIFEST}" DB_GENERATED_FILES)
if(NOT DB_GENERATED_FILES)
    message(FATAL_ERROR "${DB_MANIFEST} 为空，xml2db.py 没有产出任何文件")
endif()

# 只有 .cpp 进编译；.h / .sql 仅参与依赖与清理
set(DB_SOURCES "")
foreach(DB_FILE ${DB_GENERATED_FILES})
    if(DB_FILE MATCHES "\\.cpp$")
        list(APPEND DB_SOURCES "${DB_FILE}")
    endif()
endforeach()

if(NOT DB_SOURCES)
    message(FATAL_ERROR "${DB_TABLES_XML} 里没有表，xml2db.py 未产出任何 C++ 源文件")
endif()

# 源文件由构建系统生成：CMake 3.20 之前必须显式标记，
# 否则 clean 之后重新配置会因「找不到源文件」直接失败
set_source_files_properties(${DB_SOURCES} PROPERTIES GENERATED TRUE)

add_custom_command(
    OUTPUT ${DB_GENERATED_FILES}
    COMMAND "${DB_PYTHON_EXECUTABLE}" ${DB_GENERATOR_ARGS}
    DEPENDS "${DB_TABLES_XML}" "${DB_GENERATOR}"
    COMMENT "xml2db: 从 tables.xml 生成 DDL 与 C++ 访问层"
    VERBATIM
)

# 让 .h / .sql 也进入构建图：任一产物缺失都会触发重生成
add_custom_target(db_codegen DEPENDS ${DB_GENERATED_FILES})

add_library(db STATIC ${DB_SOURCES})
add_dependencies(db db_codegen)

# 生成物以 <table>.h 的形式被包含，因此把 dbtables/ 作为 PUBLIC 包含目录暴露给消费方
target_include_directories(db PUBLIC ${DB_TABLES_DIR})

# base 提供 mysql_conn.h / utils.h，以及 MYSQLPP_INCLUDE_DIR（mysql++.h）。
# 生成的 <table>.h 里出现 mysqlpp::Query / mysqlpp::Row，故必须是 PUBLIC。
target_link_libraries(db PUBLIC base)

# make clean 连带删除生成物。ADDITIONAL_CLEAN_FILES 是 CMake 3.15 才有的目录属性
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.15)
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES ${DB_GENERATED_FILES})
else()
    message(WARNING "CMake ${CMAKE_VERSION} 低于 3.15，make clean 不会删除 xml2db 的生成物")
endif()

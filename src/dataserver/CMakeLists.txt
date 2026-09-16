file(GLOB DATASERVER_SOURCES "*.cpp")
add_executable(dataserver ${DATASERVER_SOURCES})
# db 提供 all_table_metas.h（由 xml2db 生成），它同时把 dbtables/ 作为 PUBLIC 包含目录带进来
target_link_libraries(dataserver PRIVATE base db mysqlclient)
target_include_directories(dataserver PRIVATE ${MYSQL_INCLUDE_DIR})

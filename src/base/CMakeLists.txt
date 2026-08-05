# 源文件
file(GLOB BASE_SOURCES "*.cpp")

# 移除 session_manager.cpp，因为它的实现已经移到了头文件中
list(REMOVE_ITEM BASE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/session_manager.cpp")

# 创建静态库
add_library(base STATIC ${BASE_SOURCES})

# 包含目录
target_include_directories(base
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${MYSQL_INCLUDE_DIR}
        ${MYSQLPP_INCLUDE_DIR}
)

# 编译选项（Debug/Release 自动适配，由 compiler_options.cmake 控制全局）
target_compile_options(base PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-function
    -Wno-error=unused-function
    -Wno-reorder
)

# Debug 构建时启用调试信息和优化关闭
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(base PRIVATE -g -O0)
    target_compile_definitions(base PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
else()
    target_compile_options(base PRIVATE -O2 -DNDEBUG)
    target_compile_definitions(base PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
endif()

# 链接库
target_link_libraries(base
    PUBLIC
        spdlog::spdlog
        yaml-cpp
        Threads::Threads
        ${Boost_LIBRARIES}
        protos
        ${MYSQL_LIBRARY}
        ${MYSQLPP_LIBRARY}
)

# 平台相关：崩溃处理
# Linux: -rdynamic 让 backtrace_symbols 能解析函数名（通过 INTERFACE 传播给可执行文件）
# Windows: 链接 dbghelp（MiniDumpWriteDump）
if(WIN32)
    target_link_libraries(base PUBLIC dbghelp)
else()
    target_link_libraries(base INTERFACE "-Wl,--export-dynamic")
endif()

# asio 是 boost 的一部分，定义宏即可
target_compile_definitions(base
    PUBLIC
        ASIO_STANDALONE
)
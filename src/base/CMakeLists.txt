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
        ${THIRD_PARTY_INCLUDE_DIR}
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

# 平台相关：崩溃处理 + 堆栈打印
#
# boost::stacktrace 配置（header-only 模式，不定义 BOOST_STACKTRACE_LINK）：
#   不需要链接 libboost_stacktrace_*.a / .lib，只需系统级依赖
#
# Windows: 链接 dbghelp（MiniDumpWriteDump + boost::stacktrace 符号解析）
#          默认 WINDBG backend 用 SymFromAddr + SymGetLineFromAddr64 解析 file:line
#          MSVC 链接器加 /DEBUG 才生成 .pdb（无 PDB 拿不到行号）
# Linux:   BOOST_STACKTRACE_USE_ADDR2LINE backend
#          运行时 fork /usr/bin/addr2line 解析 file:line（需系统装了 binutils）
#          -Wl,--export-dynamic 让 dladdr 能解析函数名
#          ${CMAKE_DL_LIBS} 提供 dladdr
if(WIN32)
    target_link_libraries(base PUBLIC dbghelp)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # 仅 Debug 模式生成 .pdb（Release 生成完整 PDB 链接很慢）
        target_link_options(base INTERFACE /DEBUG)
    endif()
else()
    target_link_libraries(base INTERFACE "-Wl,--export-dynamic" ${CMAKE_DL_LIBS})
    target_compile_definitions(base PUBLIC BOOST_STACKTRACE_USE_ADDR2LINE)
endif()

# asio 是 boost 的一部分，定义宏即可
target_compile_definitions(base
    PUBLIC
        ASIO_STANDALONE
)
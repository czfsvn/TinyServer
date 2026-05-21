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
)

target_compile_options(base PRIVATE
    -O2
    -DNDEBUG
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-function
    -Wno-error=unused-function
    -Wno-reorder
)


# 链接库
target_link_libraries(base
    PUBLIC
        spdlog::spdlog
        Threads::Threads
        ${Boost_LIBRARIES}
        protos
)

# asio 是 boost 的一部分，定义宏即可
target_compile_definitions(base
    PUBLIC
        ASIO_STANDALONE  # 如果使用 standalone asio 才需要，用 boost.asio 则不需要
)
# CMakeLists.txt for tools module - 直接编译示例程序，不创建静态库

# Build log_reader_example
add_executable(log_reader_example
    logs/log_reader_example.cpp
    logs/log_reader.cpp
)

# 包含目录
target_include_directories(log_reader_example
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/logs
        ${CMAKE_SOURCE_DIR}/base
)

# 编译选项
target_compile_options(log_reader_example PRIVATE
    -O2
    -DNDEBUG
    -Wall
    -Wextra
    -Wpedantic
)

# 链接库
target_link_libraries(log_reader_example
    PRIVATE
        base
        spdlog::spdlog
        Threads::Threads
        ${Boost_LIBRARIES}
)

# Install
install(TARGETS log_reader_example
    RUNTIME DESTINATION bin
)

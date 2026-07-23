cmake_minimum_required(VERSION 3.10)
project(NetworkFramework VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++ 标准为 17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加自定义 CMake 模块路径
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# 设置输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 设置构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Source directory: ${CMAKE_SOURCE_DIR}")
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")

# 包含自定义查找模块
include(${CMAKE_SOURCE_DIR}/../cmake/compiler_options.cmake)
include(${CMAKE_SOURCE_DIR}/../cmake/find_dependencies.cmake)

add_subdirectory(config)
add_subdirectory(protos)
add_subdirectory(base)
add_subdirectory(examples)
add_subdirectory(tinyserver)    
add_subdirectory(dataserver)
add_subdirectory(tinyclient)
add_subdirectory(gateway)
#add_subdirectory(tools)

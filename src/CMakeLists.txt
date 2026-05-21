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

# 根据 C++ 17 特性设置编译选项
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0 -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
    else()
        add_compile_options(-O2 -DNDEBUG -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0 -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
    else()
        add_compile_options(-O2 -DNDEBUG -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/W4 /permissive-)
    
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/Zi /Od /RTC1)
    else()
        add_compile_options(/O2 /DNDEBUG)
    endif()
endif()

# 查找 Boost
find_package(Boost 1.85.0 REQUIRED COMPONENTS system thread program_options)
if(Boost_FOUND)
    message(STATUS "Boost found: ${Boost_VERSION}")
    message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
    message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
    
    # 为 C++17 设置 Boost 宏
    if(Boost_VERSION VERSION_GREATER_EQUAL 1.67)
        add_definitions(-DBOOST_ASIO_HAS_STD_INVOKE_RESULT)
        add_definitions(-DBOOST_ASIO_HAS_STD_STRING_VIEW)
    endif()
else()
    message(FATAL_ERROR "Boost not found. Please install Boost and set BOOST_ROOT environment variable.

To install Boost on Windows:
1. Download Boost from https://www.boost.org/users/download/
2. Extract to C:\Boost
3. Set BOOST_ROOT environment variable to C:\Boost

To install Boost on Linux:
1. Run: sudo apt-get install libboost-all-dev (Ubuntu/Debian)
2. Or: sudo yum install boost-devel (CentOS/RHEL)")
endif()

# 查找 spdlog
find_package(spdlog REQUIRED)

if(spdlog_FOUND)
    message(STATUS "Found spdlog at: ${spdlog_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "spdlog not found. Please install spdlog to /usr/local/third/")
endif()

# 查找 Protobuf
find_package(Protobuf REQUIRED)

if(Protobuf_FOUND)
    message(STATUS "Found Protobuf at: ${Protobuf_INCLUDE_DIRS}")
    message(STATUS "Protobuf libraries: ${Protobuf_LIBRARIES}")
else()
    message(FATAL_ERROR "Protobuf not found. Please install Protobuf.")
endif()

add_subdirectory(protos)
add_subdirectory(base)
add_subdirectory(examples)
add_subdirectory(tinyserver)    
add_subdirectory(tinyclient)
add_subdirectory(gateway)
add_subdirectory(tools)

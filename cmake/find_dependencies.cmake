# 查找 Boost
find_package(Boost 1.85.0 REQUIRED COMPONENTS system filesystem thread program_options)
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

# ============================================
# 查找 MySQL 客户端库
# ============================================
find_path(MYSQL_INCLUDE_DIR mysql.h
          PATHS /usr/include/mysql /usr/local/include/mysql
          DOC "MySQL client include directory")

find_library(MYSQL_LIBRARY mysqlclient
             PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib
             DOC "MySQL client library")

if(MYSQL_INCLUDE_DIR AND MYSQL_LIBRARY)
    message(STATUS "Found MySQL include dir: ${MYSQL_INCLUDE_DIR}")
    message(STATUS "Found MySQL library: ${MYSQL_LIBRARY}")
else()
    message(FATAL_ERROR "MySQL client library not found. Please install libmysqlclient-dev.")
endif()

# ============================================
# 查找 MySQL++ (mysqlpp)
# ============================================
find_path(MYSQLPP_INCLUDE_DIR mysql++.h
          PATHS /usr/local/third/include/mysql++ /usr/include/mysql++
          DOC "MySQL++ include directory")

find_library(MYSQLPP_LIBRARY mysqlpp
             PATHS /usr/local/third/lib /usr/lib
             DOC "MySQL++ library")

if(MYSQLPP_INCLUDE_DIR AND MYSQLPP_LIBRARY)
    message(STATUS "Found MySQL++ include dir: ${MYSQLPP_INCLUDE_DIR}")
    message(STATUS "Found MySQL++ library: ${MYSQLPP_LIBRARY}")
else()
    message(FATAL_ERROR "MySQL++ not found. Please install MySQL++ to /usr/local/third/")
endif()

# 查找 Protobuf
find_package(Protobuf REQUIRED)
if(Protobuf_FOUND)
    message(STATUS "Found Protobuf at: ${Protobuf_INCLUDE_DIRS}")
    message(STATUS "Protobuf libraries: ${Protobuf_LIBRARIES}")
else()
    message(FATAL_ERROR "Protobuf not found. Please install Protobuf.")
endif()

# 查找 spdlog
find_package(spdlog REQUIRED)
if(spdlog_FOUND)
    message(STATUS "Found spdlog at: ${spdlog_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "spdlog not found. Please install spdlog to /usr/local/third/")
endif()

find_package(yaml-cpp CONFIG REQUIRED)
if(yaml-cpp_FOUND)
    message(STATUS "Found yaml-cpp at: ${yaml-cpp_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "yaml-cpp not found. Please install yaml-cpp to /usr/local/third/")
endif()

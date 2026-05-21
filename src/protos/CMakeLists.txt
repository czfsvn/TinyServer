cmake_minimum_required(VERSION 3.10)

# 查找 Protobuf
find_package(Protobuf REQUIRED)

if(Protobuf_FOUND)
    message(STATUS "Found Protobuf at: ${Protobuf_INCLUDE_DIRS}")
    message(STATUS "Protobuf libraries: ${Protobuf_LIBRARIES}")
else()
    message(FATAL_ERROR "Protobuf not found. Please install Protobuf.")
endif()

# 收集所有 .proto 文件
message(STATUS "CMAKE_CURRENT_SOURCE_DIR: ${CMAKE_CURRENT_SOURCE_DIR}")
file(GLOB_RECURSE PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.proto")

# 生成 protobuf 文件
set(GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 创建生成目录
file(MAKE_DIRECTORY ${GENERATED_DIR})

# 生成 protobuf 文件
foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    set(PROTO_HDR ${GENERATED_DIR}/${PROTO_NAME}.pb.h)
    set(PROTO_SRC ${GENERATED_DIR}/${PROTO_NAME}.pb.cc)
    
    add_custom_command(
        OUTPUT ${PROTO_HDR} ${PROTO_SRC}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        ARGS --cpp_out=${GENERATED_DIR} --proto_path=${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating ${PROTO_NAME}.pb.h and ${PROTO_NAME}.pb.cc"
    )
    
    list(APPEND PROTO_SRCS ${PROTO_SRC})
    list(APPEND PROTO_HDRS ${PROTO_HDR})
endforeach()

# 创建 protobuf 库
add_library(protos STATIC ${PROTO_SRCS})

# 包含目录
target_include_directories(protos
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${Protobuf_INCLUDE_DIRS}
        ${GENERATED_DIR}
)

# 链接库
target_link_libraries(protos
    PUBLIC
        ${Protobuf_LIBRARIES}
)

# 设置输出目录
set_target_properties(protos PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

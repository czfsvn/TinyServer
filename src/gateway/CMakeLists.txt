# # Gateway Server CMakeLists.txt

# set(TARGET_NAME gateway)

# # 添加源文件
# set(SOURCES
#     main.cpp
#     gateway_server.cpp
#     gate_task.cpp
#     gate_task_manager.cpp
#     gate_user.cpp
#     gate_user_manager.cpp
# )

# # 添加头文件
# set(HEADERS
#     gateway_server.h
#     gate_task.h
#     gate_task_manager.h
#     gate_user.h
#     gate_user_manager.h
# )

# # 创建可执行文件
# add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

# # 链接依赖库
# target_link_libraries(${TARGET_NAME}
#     base
#     ${Boost_LIBRARIES}
#     ${PROTOBUF_LIBRARIES}
# )

# # 设置输出目录
# set_target_properties(${TARGET_NAME} PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
# )



file(GLOB SOURCES "*.cpp")
add_executable(gateserver ${SOURCES})
target_link_libraries(gateserver PRIVATE base)


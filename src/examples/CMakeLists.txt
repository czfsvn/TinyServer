# add_executable(network_example network_example.cpp)
# target_link_libraries(network_example PRIVATE base)

# add_executable(singleton_example singleton_example.cpp)
# target_link_libraries(singleton_example PRIVATE base)

# add_executable(timer_example timer_example.cpp)
# target_link_libraries(timer_example PRIVATE base)

add_executable(timer_manager_example timer_manager_example.cpp)
target_link_libraries(timer_manager_example PRIVATE base)

#add_executable(network_test network_test.cpp)
#target_link_libraries(network_test PRIVATE base)
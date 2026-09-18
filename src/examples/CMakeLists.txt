# add_executable(network_example network_example.cpp)
# target_link_libraries(network_example PRIVATE base)

# add_executable(singleton_example singleton_example.cpp)
# target_link_libraries(singleton_example PRIVATE base)

# add_executable(timer_example timer_example.cpp)
# target_link_libraries(timer_example PRIVATE base)

#add_executable(timer_manager_example timer_manager_example.cpp)
#target_link_libraries(timer_manager_example PRIVATE base)

#add_executable(network_test network_test.cpp)
#target_link_libraries(network_test PRIVATE base)

add_executable(readxml readxml.cpp)
target_link_libraries(readxml PRIVATE base)

# 时间轮行为验证程序: 跑 runAfter / runEvery / cancel / 回调内注册, 超出一格误差即失败
add_executable(timer_verify timer_verify.cpp)
target_link_libraries(timer_verify PRIVATE base)

# SPSC 队列行为验证程序: [5] 双线程那条只有在 TSAN 下才有意义.
# 零依赖版本(同样的用例另有一份 gtest 版, 见 src/tests/), 装不了 GoogleTest 也能跑
add_executable(spsc_queue_verify spsc_queue_verify.cpp)
target_link_libraries(spsc_queue_verify PRIVATE base)
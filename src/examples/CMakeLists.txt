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

# 队列行为验证程序: [3][5] 两条并发用例只有在 TSAN 下才有意义.
# [5] 复刻了 network.cpp 的 is_sending_ 双检握手 —— 那是本次改动唯一有真实
# 并发风险的地方(lost wakeup 偶发难复现), 所以它放在默认构建的 examples 里而不是
# 只在不带 ENABLE_TESTS 才编的 gtest 里, 免得大多数人跑不到.
# 零依赖版本(同样的用例另有一份 gtest 版, 见 src/tests/), 装不了 GoogleTest 也能跑
add_executable(message_queue_verify message_queue_verify.cpp)
target_link_libraries(message_queue_verify PRIVATE base)
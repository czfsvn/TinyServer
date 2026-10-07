# ============================================
# 单元测试（GoogleTest）
# ============================================
#
# 默认不参与构建，由顶层 src/CMakeLists.txt 的 ENABLE_TESTS 打开：
#
#     cmake -S src -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
#     cmake --build build --target message_queue_test
#     ./build/bin/message_queue_test
#     ctest --test-dir build --output-on-failure
#
# GoogleTest 的取得顺序：先用系统装的（libgtest-dev 之类）；没有就用 FetchContent
# 拉一份源码一起编。两条路都走不通就明确报错 —— 宁可配置失败，也不静默跳过测试。
#
# 注意：测试二进制也受全局编译选项影响，所以 -DENABLE_TSAN=ON 时，
# message_queue_test 同样跑在 ThreadSanitizer 下（并发用例正是为它准备的）。

set(TINY_GTEST_LIBS "")

find_package(GTest QUIET)

if(GTest_FOUND)
    # find_package(GTest) 在不同 CMake 版本里给出的 target 名不同：
    #   < 3.20：GTest::GTest + GTest::Main
    #   >=3.20：GTest::gtest + GTest::gtest_main（旧名保留为别名）
    # 所以先问新版名字，再问旧版。
    if(TARGET GTest::gtest_main)
        set(TINY_GTEST_LIBS GTest::gtest GTest::gtest_main)
    elseif(TARGET GTest::GTest AND TARGET GTest::Main)
        set(TINY_GTEST_LIBS GTest::GTest GTest::Main)
    else()
        message(FATAL_ERROR "找到了 GoogleTest，但拿不到预期的 target，请检查 find_package(GTest) 的结果")
    endif()
    message(STATUS "GoogleTest: 使用系统安装版本")

elseif(CMAKE_VERSION VERSION_GREATER_EQUAL 3.11)
    # FetchContent 需要 CMake 3.11；FetchContent_MakeAvailable 还要 3.14，
    # 这里用 Populate + add_subdirectory 的老写法，3.11 起都能用。
    include(FetchContent)

    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(GTEST_TAG "v1.15.2" CACHE STRING "googletest tag to fetch")

    FetchContent_Declare(
        googletest
        URL "https://github.com/google/googletest/archive/refs/tags/${GTEST_TAG}.tar.gz"
    )

    FetchContent_GetProperties(googletest)
    if(NOT googletest_POPULATED)
        FetchContent_Populate(googletest)
        add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()

    set(TINY_GTEST_LIBS gtest gtest_main)
    message(STATUS "GoogleTest: FetchContent 拉取 ${GTEST_TAG}")

else()
    message(FATAL_ERROR
        "没找到 GoogleTest，且当前 CMake ${CMAKE_VERSION} 低于 3.11 无法自动拉取。请三选一：\n"
        "  [1] apt-get install -y libgtest-dev 后重新配置\n"
        "  [2] 升级 CMake 到 >= 3.11（配置时可访问网络，会自动拉 googletest）\n"
        "  [3] 离线：手动下载 googletest 源码，让 find_package(GTest) 能找到它")
endif()

add_executable(message_queue_test message_queue_test.cpp)
target_link_libraries(message_queue_test PRIVATE base ${TINY_GTEST_LIBS})

# 用 add_test 而不是 gtest_discover_tests()：
# 后者要在构建期跑一遍二进制去收集用例清单，配 sanitizer 时没必要多这一趟。
#
# tiny_add_test(<target>) 统一包掉两件跟 sanitizer 有关的事：
#
# 1. TSAN + 新内核（6.6+ 把 vm.mmap_rnd_bits 提到 32）的组合会让旧的 TSAN 运行时
#    在启动时直接 "FATAL: unexpected memory mapping"。容器里改不了宿主机 sysctl，
#    用 setarch -R（对本进程关闭 ASLR）绕开；宿主机层面也可以
#    sysctl -w vm.mmap_rnd_bits=28，一劳永逸。
# 2. ASAN 的 detect_leaks 跟着 ENABLE_ASAN_LEAKS 走（开关见 compiler_options.cmake）。
#
# 调用放在文件末尾：那时候所有 target 都已经定义完了。

# ---- 先探测：TSAN 到底要不要包 setarch ----
#
# 两个条件都满足才包：
#   (a) 真的需要：vm.mmap_rnd_bits >= 32（内核 6.6+ 的默认值）会让旧的 TSAN
#       运行时启动时直接 "FATAL: unexpected memory mapping"。值是 28 的话没这问题。
#   (b) 真的能用：setarch 要在容器里跑一次才知道 —— Docker 默认 seccomp 拦
#       personality 系统调用，命令存在也照样 "不允许的操作"。
# 之前的实现是 (b) 成立就无条件包，结果在这台容器上三个测试全挂，
# 而实际上 mmap_rnd_bits=28 根本不需要它。
set(TINY_TSAN_NEEDS_SETARCH FALSE)

if(ENABLE_TSAN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(TINY_SETARCH_EXE setarch)

    if(NOT TINY_SETARCH_EXE)
        message(STATUS "TSAN: 没找到 setarch")
    else()
        execute_process(
            COMMAND ${TINY_SETARCH_EXE} ${CMAKE_SYSTEM_PROCESSOR} -R /bin/true
            RESULT_VARIABLE _setarch_rc
            OUTPUT_QUIET
            ERROR_QUIET)

        if(NOT _setarch_rc EQUAL 0)
            message(STATUS "TSAN: setarch 在当前环境不可用（多半是容器 seccomp 拦了 "
                           "personality），直接用原样命令跑")
        elseif(NOT EXISTS "/proc/sys/vm/mmap_rnd_bits")
            message(STATUS "TSAN: 读不到 /proc/sys/vm/mmap_rnd_bits，跳过 setarch 判断")
        else()
            file(READ "/proc/sys/vm/mmap_rnd_bits" _rnd_bits)
            string(STRIP "${_rnd_bits}" _rnd_bits)

            if(_rnd_bits GREATER_EQUAL 32)
                set(TINY_TSAN_NEEDS_SETARCH TRUE)
                message(STATUS "TSAN: vm.mmap_rnd_bits=${_rnd_bits}，用 setarch -R 关闭 ASLR")
            else()
                message(STATUS "TSAN: vm.mmap_rnd_bits=${_rnd_bits}，不需要 setarch")
            endif()
        endif()
    endif()
endif()

function(tiny_add_test target)
    set(_cmd $<TARGET_FILE:${target}>)

    if(TINY_TSAN_NEEDS_SETARCH)
        set(_cmd ${TINY_SETARCH_EXE} ${CMAKE_SYSTEM_PROCESSOR} -R ${_cmd})
    endif()

    add_test(NAME ${target} COMMAND ${_cmd})

    if(ENABLE_ASAN)
        set_tests_properties(${target} PROPERTIES
            ENVIRONMENT "ASAN_OPTIONS=${TINY_ASAN_OPTIONS}")
    endif()
endfunction()

# 可行性探针：验证 acceptor.async_accept(io_context&, token) 能否把新连接直接投递到目标 io_context
# 不依赖 GoogleTest，只要 ENABLE_TESTS=ON 就会一起编译出来
add_executable(io_context_accept_probe io_context_accept_probe.cpp)
target_link_libraries(io_context_accept_probe PRIVATE base)

# 并发探针：N 个 io_context / 每个 1 线程 / 每个 context 上同时挂多个 Session，
# 检测同一 context 内 handler 是否重叠执行（单线程串行保证）
add_executable(io_context_session_probe io_context_session_probe.cpp)
target_link_libraries(io_context_session_probe PRIVATE base)

# 真实流量压测客户端：对 tinyserver 建 N 条连接、每条发 R 帧并校验回显。
# 重点是压「tick 线程 -> Session::send() -> post 回 worker context」这条跨线程路径（N4）。
add_executable(tinyserver_load_probe tinyserver_load_probe.cpp)
target_link_libraries(tinyserver_load_probe PRIVATE base)

# 广播共享探针：验证同一份序列化结果被 N 个 Session 共享（use_count 逐个 +1），
# 而不是每个接收者一份拷贝。不依赖外部服务端，可直接进 ctest。
add_executable(broadcast_share_probe broadcast_share_probe.cpp)
target_link_libraries(broadcast_share_probe PRIVATE base)

# ---- 注册 ctest 用例 ----
#
# 两个探针都是自包含的（自己起服务端和客户端、跑完自己退出，失败返回非 0），
# 所以可以直接进 ctest。其中 io_context_session_probe 尤其该在 TSAN 下跑 ——
# 它验证的正是「N 个 context / 每 context 多 Session」有没有数据竞争。
#
# tinyserver_load_probe 不注册：它要连一个外部已经在跑的 tinyserver，
# ctest 起不来那个服务端。用法见文件头注释。
tiny_add_test(message_queue_test)
tiny_add_test(io_context_accept_probe)
tiny_add_test(io_context_session_probe)
tiny_add_test(broadcast_share_probe)

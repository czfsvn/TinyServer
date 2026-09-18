# ============================================
# 单元测试（GoogleTest）
# ============================================
#
# 默认不参与构建，由顶层 src/CMakeLists.txt 的 ENABLE_TESTS 打开：
#
#     cmake -S src -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
#     cmake --build build --target spsc_queue_test
#     ./build/bin/spsc_queue_test
#     ctest --test-dir build --output-on-failure
#
# GoogleTest 的取得顺序：先用系统装的（libgtest-dev 之类）；没有就用 FetchContent
# 拉一份源码一起编。两条路都走不通就明确报错 —— 宁可配置失败，也不静默跳过测试。
#
# 注意：测试二进制也受全局编译选项影响，所以 -DENABLE_TSAN=ON 时，
# spsc_queue_test 同样跑在 ThreadSanitizer 下（这正是 [5] 那条用例想要的）。

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

add_executable(spsc_queue_test spsc_queue_test.cpp)
target_link_libraries(spsc_queue_test PRIVATE base ${TINY_GTEST_LIBS})

# 用 add_test 而不是 gtest_discover_tests()：
# 后者要在构建期跑一遍二进制去收集用例清单，配 sanitizer 时没必要多这一趟
#
# TSAN + 新内核（6.6+ 把 vm.mmap_rnd_bits 提到 32）的组合会让旧的 TSAN 运行时
# 在启动时直接 "FATAL: unexpected memory mapping"。容器里改不了宿主机 sysctl，
# 用 setarch -R（对本进程关闭 ASLR）绕开；宿主机层面也可以
# sysctl -w vm.mmap_rnd_bits=28，一劳永逸。
if(ENABLE_TSAN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(SETARCH_EXE setarch)
    if(SETARCH_EXE)
        add_test(NAME spsc_queue_test
                 COMMAND ${SETARCH_EXE} ${CMAKE_SYSTEM_PROCESSOR} -R
                         $<TARGET_FILE:spsc_queue_test>)
    else()
        message(WARNING "ENABLE_TSAN 下没找到 setarch；内核 >= 6.6 时测试会因 ASLR 无法启动")
        add_test(NAME spsc_queue_test COMMAND spsc_queue_test)
    endif()
else()
    add_test(NAME spsc_queue_test COMMAND spsc_queue_test)
endif()

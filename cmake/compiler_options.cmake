# ============================================
# 编译器选项配置
# ============================================

# ============================================
# Sanitizer 开关
# ============================================
#
# 三个开关可单独开，也允许 ASAN + UBSAN 组合；TSAN 与 ASAN 互斥（下面强制检查）。
#
#   cmake -S src -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
#   cmake -S src -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_TSAN=ON
#
# TSAN 用来验证无锁结构 —— 消息队列（MyConcurrentQueue）与 Session::send() 的
# kick 握手都不持锁，正确性全靠原子操作建立 happens-before。这类错误单线程用例
# 测不出来（握手那个 lost wakeup 更是只在特定时序下偶发），必须跑 ThreadSanitizer。
# 它同时也是验证「N 个 io_context / 每 context 多 Session」有没有数据竞争的唯一手段。
#
# ASAN 用来抓越界读写与 use-after-free：Session 的生命周期跨 io 线程与 tick 线程，
# 哪条路径漏了 post，写完成 handler 就可能摸到已经释放的 buffer。
#
# 注意：protobuf / mysqlpp 是预编译的、没带插桩的 .so。TSAN 报在它们内部的 race
# 基本都是假阳性，用 TSAN_OPTIONS=ignore_noninstrumented_modules=1 屏蔽。
option(ENABLE_ASAN  "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN  "Enable ThreadSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

# ASAN 自带 LeakSanitizer，Linux 上默认开。服务端有一批故意活到进程退出的单例
# （sTinyServerConfig / sTinyTaskManager / sIOContextPool 等），会报一堆预期内的
# "泄漏"。想先只看内存破坏就把这项关掉（等价于 ASAN_OPTIONS=detect_leaks=0）。
option(ENABLE_ASAN_LEAKS "Enable LeakSanitizer (bundled with ASAN)" ON)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR
        "ENABLE_ASAN 与 ENABLE_TSAN 互斥：两套运行时都要拦截同一批内存/线程原语，"
        "同时开会互相踩。请分成两个 build 目录各开一个（build-asan / build-tsan）。")
endif()

set(TINY_SANITIZERS "")
if(ENABLE_ASAN)
    list(APPEND TINY_SANITIZERS address)
endif()
if(ENABLE_TSAN)
    list(APPEND TINY_SANITIZERS thread)
endif()
if(ENABLE_UBSAN)
    list(APPEND TINY_SANITIZERS undefined)
endif()

# 给 ctest 用：add_test(... ENVIRONMENT "ASAN_OPTIONS=${TINY_ASAN_OPTIONS}")
if(ENABLE_ASAN_LEAKS)
    set(TINY_ASAN_OPTIONS "detect_leaks=1")
else()
    set(TINY_ASAN_OPTIONS "detect_leaks=0")
endif()

# 根据编译器类型设置编译选项
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
        add_compile_options(/Zi /Od)
        # /fsanitize=address 与 /RTC1 不兼容（MSVC 直接报 D8016），开 ASAN 时不能加
        if(NOT ENABLE_ASAN)
            add_compile_options(/RTC1)
        endif()
    else()
        add_compile_options(/O2 /DNDEBUG)
    endif()
endif()

# ============================================
# 应用 sanitizer
# ============================================
#
# 用 CMAKE_EXE_LINKER_FLAGS 而非 add_link_options()：后者要 CMake 3.13，
# 而本工程声明的是 cmake_minimum_required(3.10)。
# 链接期同样要带 -fsanitize，否则运行时库不会链进来，报一堆 undefined symbol。
if(TINY_SANITIZERS)
    string(REPLACE ";" "," TINY_SANITIZER_LIST "${TINY_SANITIZERS}")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        add_compile_options(-fsanitize=${TINY_SANITIZER_LIST})
        # 没有帧指针，ASAN/TSAN 打出来的栈回溯会缺帧
        add_compile_options(-fno-omit-frame-pointer)
        if(ENABLE_ASAN)
            # 尾调用优化会把出错函数的栈帧合并掉，回溯直接指到错的地方
            add_compile_options(-fno-optimize-sibling-calls)
        endif()

        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=${TINY_SANITIZER_LIST}")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=${TINY_SANITIZER_LIST}")
        message(STATUS "Sanitizers enabled: ${TINY_SANITIZER_LIST}")

        if(ENABLE_ASAN AND NOT ENABLE_ASAN_LEAKS)
            message(STATUS "  LeakSanitizer 已关闭（ENABLE_ASAN_LEAKS=OFF）")
        endif()
        if(ENABLE_UBSAN)
            message(STATUS "  UBSAN 默认只打印不中断；要一命中就中止："
                           "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1")
        endif()
    else()
        # MSVC 只有 AddressSanitizer（VS2019 16.9+），thread/undefined 都没有
        if(ENABLE_ASAN)
            add_compile_options(/fsanitize=address)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /fsanitize=address")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /fsanitize=address")
            message(STATUS "Sanitizers enabled: address (MSVC)")
        endif()
        foreach(san IN LISTS TINY_SANITIZERS)
            if(NOT san STREQUAL "address")
                message(WARNING "MSVC 只支持 AddressSanitizer，'${san}' 被忽略")
            endif()
        endforeach()
    endif()
endif()

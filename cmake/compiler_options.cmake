# ============================================
# 编译器选项配置
# ============================================

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
        add_compile_options(/Zi /Od /RTC1)
    else()
        add_compile_options(/O2 /DNDEBUG)
    endif()
endif()
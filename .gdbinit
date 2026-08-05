# ============================================
# TinyServer GDB 初始化脚本
# ============================================
# 用法：
#   1. 复制到 ~/.gdbinit 自动加载
#   2. 或启动时加载: gdb -x .gdbinit gateserver
#   3. 或在 GDB 内: source .gdbinit

# ============================================
# 1. 跳过 STL 标准库函数（避免进入 allocator 等内部实现）
# ============================================

# 跳过 std:: 分配器和容器实现
skip function std::allocator
skip function std::new_allocator
skip function std::__allocator_basic
skip function std::__alloc_traits
skip function std::__detail::_Vector_base
skip function std::__detail::_List_node
skip function std::__detail::_Map_base
skip function std::__detail::_Rb_tree
skip function std::_Rb_tree
skip function std::string
skip function std::basic_string
skip function std::shared_ptr
skip function std::unique_ptr
skip function std::make_shared
skip function std::make_unique
skip function std::tuple
skip function std::pair
skip function std::function
skip function std::mutex
skip function std::lock_guard
skip function std::unique_lock
skip function std::condition_variable
skip function std::thread

# 跳过 std::filesystem
skip function std::filesystem::directory_iterator
skip function std::filesystem::path

# 跳过 std::chrono
skip function std::chrono::system_clock
skip function std::chrono::steady_clock
skip function std::chrono::duration

# ============================================
# 2. 跳过第三方库
# ============================================

# yaml-cpp
skip function YAML::detail::node_data
skip function YAML::detail::memory_holder
skip function YAML::NodeData
skip function YAML::BadConversion
skip function YAML::BadSubscript

# spdlog
skip function spdlog::logger
skip function spdlog::async_logger
skip function spdlog::sinks::stdout_color_sink_mt
skip function spdlog::sinks::basic_file_sink_mt
skip function spdlog::sinks::base_sink
skip function spdlog::details::file_helper

# boost
skip function boost::asio::io_context
skip function boost::asio::ip::tcp
skip function boost::beast::websocket

# protobuf
skip function google::protobuf::Message
skip function google::protobuf::internal

# mysql
skip function MYSQL
skip function mysqlpp

# ============================================
# 3. 调试配置
# ============================================

set print pretty on
set print object on
set print static-members off
set print vtbl on
set pagination off

# ============================================
# 4. 自定义命令
# ============================================

# pstr: 打印字符串（支持 std::string 和 char*）
define pstr
    set $p = (char*)$arg0
    if $p != 0
        printf "%s\n", $p
    else
        printf "(null)\n"
    end
end
document pstr
打印字符串内容
用法: pstr <char* 指针>
end

# pbs: 打印 std::string
define pbs
    set $s = (std::string*)$arg0
    if $s != 0
        printf "%s\n", $s->c_str()
    end
end
document pbs
打印 std::string 内容
用法: pbs <std::string* 指针>
end

# bt10: 短回溯（10 层）
define bt10
    bt 10
end

# thbt: 所有线程回溯
define thbt
    thread apply all bt 5
end
document thbt
打印所有线程的调用栈（每个 5 层）
end

# ============================================
# 5. 提示信息
# ============================================
printf "\n"
printf "========================================\n"
printf " TinyServer GDB 环境已加载\n"
printf "\n"
printf " 快捷键：\n"
printf "   s/step   - 步入（已跳过 STL 函数）\n"
printf "   n/next   - 单步执行（跳过函数调用）\n"
printf "   finish   - 跳出当前函数\n"
printf "   c        - 继续运行\n"
printf "   bt10     - 短回溯\n"
printf "   thbt     - 所有线程回溯\n"
printf "   pstr     - 打印字符串\n"
printf "   skip list - 查看跳过的函数列表\n"
printf "========================================\n"
printf "\n"
#ifndef CRASH_HANDLER_H
#define CRASH_HANDLER_H

#include <string>

namespace cncpp
{
    /// 崩溃处理：进程崩溃时生成 dump + 调用栈文本
    ///
    /// 与 SignalHandler 分离：
    /// - 崩溃信号/异常用平台原生同步机制，不走 io_context
    /// - 普通 SIGINT/SIGTERM 仍由 SignalHandler 经 asio signal_set 异步处理
    ///
    /// Linux:
    ///   sigaction 注册 SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS/SIGPIPE
    ///   解除 RLIMIT_CORE，尝试写 /proc/sys/kernel/core_pattern
    ///   handler 里 backtrace 写工作目录，重新 raise 让内核生成 core
    ///
    /// Windows:
    ///   SetUnhandledExceptionFilter + CRT 错误处理
    ///   MiniDumpWriteDump 生成 .dmp，CaptureStackBackTrace 写栈文本
    ///
    /// 产物落在进程工作目录，文件名 crash_<pid>_<时间戳>.{log,dmp}
    class CrashHandler
    {
    public:
        /// 初始化崩溃处理（注册信号/异常 handler，解除 core 限制）
        /// @param dump_dir 产物输出目录，空字符串表示进程工作目录
        static void init(const std::string& dump_dir = "");

    private:
        CrashHandler() = delete;
    };

}  // namespace cncpp

#endif  // CRASH_HANDLER_H

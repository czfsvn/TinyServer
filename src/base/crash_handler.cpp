#include "crash_handler.h"
#include "logger.h"

#ifndef _WIN32
// ============================================================================
// Linux 实现：sigaction 同步 handler + RLIMIT_CORE + backtrace + 内核 core
// ============================================================================

#include <execinfo.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace
{
    const char* signalName(int sig)
    {
        switch (sig)
        {
            case SIGSEGV:
                return "SIGSEGV";
            case SIGABRT:
                return "SIGABRT";
            case SIGFPE:
                return "SIGFPE";
            case SIGILL:
                return "SIGILL";
            case SIGBUS:
                return "SIGBUS";
            case SIGPIPE:
                return "SIGPIPE";
            default:
                return "UNKNOWN";
        }
    }

    /// async-signal-safe 崩溃处理函数
    /// 只使用 async-signal-safe 函数：write/open/close/snprintf/memset/
    /// sigaction/raise/backtrace/backtrace_symbols_fd/localtime_r/time/getpid
    void posixCrashHandler(int sig, siginfo_t* /*info*/, void* /*context*/)
    {
        pid_t     pid = getpid();
        time_t    now = time(nullptr);
        struct tm tm_val;
        localtime_r(&now, &tm_val);

        // 1. stderr 简短信息
        char errbuf[256];
        int  elen = snprintf(errbuf, sizeof(errbuf),
                             "\n*** Crash: signal %d (%s), pid %d, "
                              "%04d-%02d-%02d %02d:%02d:%02d ***\n",
                             sig, signalName(sig), (int)pid, tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
                             tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);
        if (elen > 0)
            write(STDERR_FILENO, errbuf, elen);

        // 2. backtrace 写文件
        void* frames[64];
        int   nframes = backtrace(frames, 64);

        char fname[256];
        snprintf(fname, sizeof(fname), "crash_%d_%04d%02d%02d_%02d%02d%02d.log", (int)pid, tm_val.tm_year + 1900,
                 tm_val.tm_mon + 1, tm_val.tm_mday, tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);

        int fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0)
        {
            char header[256];
            int hlen = snprintf(header, sizeof(header), "=== Crash: signal %d (%s), pid %d ===\n", sig, signalName(sig),
                                (int)pid);
            if (hlen > 0)
                write(fd, header, hlen);
            backtrace_symbols_fd(frames, nframes, fd);
            close(fd);
        }
        else
        {
            // open 失败，退回 stderr
            backtrace_symbols_fd(frames, nframes, STDERR_FILENO);
        }

        // 3. 恢复默认 handler 并重新 raise，让内核生成 core
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigaction(sig, &sa, nullptr);
        raise(sig);
    }
}  // anonymous namespace

void cncpp::CrashHandler::init(const std::string& /*dump_dir*/)
{
    // 解除 RLIMIT_CORE 限制
    struct rlimit rlim;
    rlim.rlim_cur = RLIM_INFINITY;
    rlim.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_CORE, &rlim) == 0)
    {
        LOG_INFO("Core dump enabled (RLIMIT_CORE = infinity)");
    }
    else
    {
        LOG_WARN("Failed to set RLIMIT_CORE: {}", strerror(errno));
    }

    // 尝试写 core_pattern（需要 root 权限，非 root 静默失败）
    int fd = open("/proc/sys/kernel/core_pattern", O_WRONLY);
    if (fd >= 0)
    {
        const char pattern[] = "core.%e.%p.%t";
        if (write(fd, pattern, sizeof(pattern) - 1) > 0)
        {
            LOG_INFO("core_pattern set to: {}", pattern);
        }
        close(fd);
    }

    // 注册崩溃信号（sigaction 同步处理）
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = posixCrashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;  // 三参数 handler

    const int crash_sigs[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, SIGPIPE};
    for (int s : crash_sigs)
    {
        if (sigaction(s, &sa, nullptr) == 0)
        {
            LOG_INFO("Crash handler registered for signal {} ({})", s, signalName(s));
        }
    }

    LOG_INFO("CrashHandler initialized (Linux)");
}

#else
// ============================================================================
// Windows 实现：SetUnhandledExceptionFilter + MiniDumpWriteDump + 调用栈
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <dbghelp.h>
#include <windows.h>

#ifdef _MSC_VER
#pragma comment(lib, "dbghelp.lib")
#endif

namespace
{
    std::string formatTimestamp()
    {
        time_t    now = time(nullptr);
        struct tm tm_val;
        localtime_s(&tm_val, &now);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm_val);
        return buf;
    }

    const char* getExceptionName(DWORD code)
    {
        switch (code)
        {
            case EXCEPTION_ACCESS_VIOLATION:
                return "ACCESS_VIOLATION";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                return "ARRAY_BOUNDS_EXCEEDED";
            case EXCEPTION_DATATYPE_MISALIGNMENT:
                return "DATATYPE_MISALIGNMENT";
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:
                return "FLT_DIVIDE_BY_ZERO";
            case EXCEPTION_FLT_OVERFLOW:
                return "FLT_OVERFLOW";
            case EXCEPTION_FLT_UNDERFLOW:
                return "FLT_UNDERFLOW";
            case EXCEPTION_ILLEGAL_INSTRUCTION:
                return "ILLEGAL_INSTRUCTION";
            case EXCEPTION_IN_PAGE_ERROR:
                return "IN_PAGE_ERROR";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                return "INT_DIVIDE_BY_ZERO";
            case EXCEPTION_INT_OVERFLOW:
                return "INT_OVERFLOW";
            case EXCEPTION_STACK_OVERFLOW:
                return "STACK_OVERFLOW";
            default:
                return "UNKNOWN";
        }
    }

    LONG WINAPI winExceptionFilter(EXCEPTION_POINTERS* ep)
    {
        DWORD       pid  = GetCurrentProcessId();
        DWORD       tid  = GetCurrentThreadId();
        std::string ts   = formatTimestamp();
        DWORD       code = ep->ExceptionRecord->ExceptionCode;

        // 1. 生成 minidump
        std::string dmpName = "crash_" + std::to_string(pid) + "_" + ts + ".dmp";
        HANDLE      hFile
            = CreateFileA(dmpName.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            mei.ThreadId          = tid;
            mei.ExceptionPointers = ep;
            mei.ClientPointers    = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), pid, hFile,
                              static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo), &mei, nullptr,
                              nullptr);
            CloseHandle(hFile);
        }

        // 2. 写调用栈文本
        std::string logName = "crash_" + std::to_string(pid) + "_" + ts + ".log";
        FILE*       fp      = nullptr;
        fopen_s(&fp, logName.c_str(), "w");
        if (fp)
        {
            fprintf(fp, "=== Crash: %s (0x%08X), pid %lu, tid %lu ===\n", getExceptionName(code), code, pid, tid);

            void*  frames[64];
            USHORT nframes = CaptureStackBackTrace(0, 64, frames, nullptr);
            fprintf(fp, "Backtrace (%u frames):\n", nframes);
            for (USHORT i = 0; i < nframes; i++)
            {
                fprintf(fp, "  [%u] 0x%p\n", i, frames[i]);
            }
            fclose(fp);
        }

        // 3. stderr
        fprintf(stderr, "\n*** Crash: %s (0x%08X), pid %lu ***\n", getExceptionName(code), code, pid);
        fprintf(stderr, "*** Minidump: %s ***\n", dmpName.c_str());
        fprintf(stderr, "*** Backtrace: %s ***\n", logName.c_str());

        return EXCEPTION_EXECUTE_HANDLER;  // 终止进程
    }

    void __cdecl invalidParameterHandler(const wchar_t* /*expression*/, const wchar_t* /*function*/,
                                         const wchar_t* /*file*/, unsigned int /*line*/, uintptr_t /*pReserved*/)
    {
        // 转成异常让 winExceptionFilter 捕获并生成 dump
        RaiseException(0xE0000001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }

    int __cdecl pureCallHandler()
    {
        RaiseException(0xE0000002, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 0;
    }
}  // anonymous namespace

void cncpp::CrashHandler::init(const std::string& /*dump_dir*/)
{
    SetUnhandledExceptionFilter(winExceptionFilter);
    _set_invalid_parameter_handler(invalidParameterHandler);
    _set_purecall_handler(pureCallHandler);
    // 清除 abort 弹窗/WER 报告，让崩溃走我们的 filter
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    LOG_INFO("CrashHandler initialized (Windows)");
}

#endif  // _WIN32

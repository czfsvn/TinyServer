#pragma once

// =============================================================================
// stacktrace.h — 主动打印调用栈到日志
//
// 用法：
//   LOG_STACK(ERROR);          // 在任意代码位置打印当前调用栈
//   LOG_STACK(WARN);
//   LOG_STACK(INFO);
//
//   // 或直接获取栈文本字符串：
//   std::string s = cncpp::captureStackTrace();
//   LOG_ERROR("unexpected: {}", s);
//
// 实现方式：
//   两平台统一使用 boost::stacktrace（header-only 模式，无需链接 boost 库）
//   Linux:   BOOST_STACKTRACE_USE_ADDR2LINE backend
//            → 运行时 fork /usr/bin/addr2line 解析 file:line
//            → 依赖: -ldl -Wl,--export-dynamic + 系统装了 addr2line (binutils)
//   Windows: 默认 WINDBG backend
//            → 用 dbghelp SymFromAddr + SymGetLineFromAddr64 解析 file:line
//            → 依赖: dbghelp.lib + .pdb 文件 (MSVC /DEBUG)
//
// 关键：不定义 BOOST_STACKTRACE_LINK，保持 header-only，不需要 libboost_stacktrace_*.a
// =============================================================================

#include <cstdint>
#include <sstream>
#include <string>

#include <boost/stacktrace.hpp>

#include "logger.h"

namespace cncpp
{
    /// 获取当前调用栈，返回格式化字符串
    /// @param skip     跳过前 N 帧（1 = 跳过 captureStackTrace 自身）
    /// @param maxDepth 最多捕获的帧数
    inline std::string captureStackTrace(std::size_t skip = 1, std::size_t maxDepth = 32)
    {
        std::ostringstream oss;

        const boost::stacktrace::stacktrace st(skip, maxDepth);

        if (st.empty())
        {
            oss << "  (no stack frames captured)";
            return oss.str();
        }

        for (std::size_t i = 0; i < st.size(); ++i)
        {
            const auto& frame = st[i];
            const auto  addr  = reinterpret_cast<std::uintptr_t>(frame.address());

            // 格式：#N [0xADDR] func_name @ file:line
            // 地址放最前面，方便手动 addr2line -e <exe> 0xADDR 解析
            oss << "  #" << i << " [0x" << std::hex << addr << std::dec << "] ";

            const auto& name = frame.name();
            if (!name.empty())
                oss << name;
            else
                oss << "(no symbol)";

            const auto& file = frame.source_file();
            if (!file.empty())
            {
                oss << " @ " << file;
                const auto line = frame.source_line();
                if (line > 0)
                    oss << ":" << line;
            }

            oss << "\n";
        }

        return oss.str();
    }
}  // namespace cncpp

// =============================================================================
// 宏接口：LOG_STACK(level)
//   level = TRACE / DEBUG / INFO / WARN / ERROR / FATAL
//   skip=1 跳过 captureStackTrace 自身，第一帧 = 调用 LOG_STACK 的函数
//
// 示例输出（每行地址在最前，方便手动 addr2line 解析）：
//   [2026-08-05 22:30:00.123] [ERROR] [tid 12345] Stack trace:
//     #0 [0x55a3b2c4d123] cncpp::GatewayServer::onMessage @ gateway_server.cpp:142
//     #1 [0x55a3b2c4d890] cncpp::Session::handlePacket @ session.cpp:88
//     #2 [0x7f1234567890] (no symbol)                  ← 动态库代码，addr2line 可查
//
// 关于行号缺失的说明（Linux + GCC 常见现象）：
//   - 函数名有但行号空 → 多半是被内联到 fmt/spdlog 等模板代码里
//     这是 GCC 模板实例化 + 优化的固有现象，-O0 也无法完全避免
//   - 完全没有函数名 → 地址在动态库里（libfmt/libstdc++/匿名 mmap）
//     手动查：addr2line -e /workspace/TinyServer/build/gateway/gateserver 0x7f1234567890
//     或：      addr2line -e /usr/lib/x86_64-linux-gnu/libfmt.so.9 0x7f1234567890
//
// 编译依赖：
//   - Linux:   -g (Debug 已有) + -Wl,--export-dynamic + 系统装 addr2line (binutils)
//   - Windows: /Zi + /DEBUG (Debug 已有) + dbghelp.lib
// =============================================================================
#define LOG_STACK(level)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        LOG_##level("Stack trace:\n{}", cncpp::captureStackTrace(1));                                                  \
    } while (0)

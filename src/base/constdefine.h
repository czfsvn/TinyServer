#ifndef CONSTDEFINE_H
#define CONSTDEFINE_H

#include <cstdint>

namespace cncpp
{
    // ======== 时间轮几何（见 ADR-0001）========
    //
    // 时间轮是一个 TIMER_SLOT_SIZE 进制的计数器：每级 TIMER_SLOT_SIZE 个槽，
    // 共 MAX_TIMER_LEVEL 级。单格时长由驱动方给出（= main_loop_interval_ms），
    // 于是
    //
    //     总跨度 = tick_ms × TIMER_SLOT_SIZE ^ MAX_TIMER_LEVEL
    //
    // 精度则恒等于 tick_ms —— 高层轮只做降级，最终都由 L1 执行，所以精度与这里
    // 两个数无关。改它们只会改变"量程度"和"多少定时器要走降级"，不会动精度。
    //
    // 当前 1000 槽 × 4 级，一格 10ms：
    //     L1 覆盖  10 秒     <- 秒级以内（心跳、超时、重连、CD 这类绝大多数定时器）
    //                           直接落 L1，一次降级都不用走
    //     L2 覆盖  2.78 小时
    //     L3 覆盖  115.7 天
    //     L4 覆盖  316.9 年  <- 总跨度
    //
    // 槽数对跨度是「级数次方」的关系，量程极容易过剩：上面 L3 的 115.7 天已经远超
    // 任何服务器需求，L4 实际是留的余量，图的是"以后不用再回来调这两个数"。
    // 挑槽数时真正该看的是 L1 的覆盖（= tick_ms × 槽数）—— 它决定多少定时器能免于
    // 降级；挑级数则是补量程，且不影响单拍开销。
    //
    // 内存：4 × 1000 = 4000 个空 list，约 96 KB。
    // 溢出边界：tick_ms × 槽数^级数 必须 < 2^64，否则 intPow() 回绕。4 级时槽数上限
    // 约 3.6 万（一格 10ms）；按 config_template.ini 的 1000ms 主循环算仍有 4 个数量级
    // 余量。加大任何一个数之前按上式核一遍。

    // 时间轮层数。跨度 = tick_ms × TIMER_SLOT_SIZE ^ MAX_TIMER_LEVEL
    inline constexpr uint16_t MAX_TIMER_LEVEL = 4;

    // 每层的槽数。同时决定 L1 能直接吃下多长的延迟（= tick_ms × 本值）。
    inline constexpr uint16_t TIMER_SLOT_SIZE = 1000;

    // ======== 主循环周期的兜底值（毫秒）========
    //
    // 一处定义，两处兜底：
    //   - IOContextPool::interval_ms_：配置没设、或读出来是 0 时用
    //   - TimerManager::init(0)：驱动方没给出有效周期时用，也就是时间轮一格
    //
    // 这两个本来就是同一个概念 —— ADR-0001 定的正是"一格 = 主循环周期"。此前写成
    // 两个常量（kDefaultIntervalMs=10 / DEFAULT_TICK_MS=50）各写各的，差 5 倍而不
    // 自知，一旦同时踩到便复现"时间轮整体走慢"那个 bug。所以这里只留一个。
    //
    // 注意：线上真正生效的是 config.ini 的 main_loop_interval_ms，这个常量只在配置
    // 缺失或为 0 时才兜底 —— 改它不会动摇已经配好的部署。
    inline constexpr uint32_t DEFAULT_MAIN_LOOP_INTERVAL_MS = 10;

    // ======== 弃载告警的日志节流（术语见 CONTEXT.md 的「弃载」）========
    //
    // 队列到顶后不是逐条打日志，而是每丢弃 DROPS_PER_WARN_LOG 条才打一条 WARN ——
    // 持续过载时逐条打，会先于真正的问题把 io 线程淹死在日志里（network.cpp 原注释）。
    // `% N == 1` 的写法保证第 1 条就报，能看见过载的起点。
    //
    // 单位是「条」不是毫秒：项目里 *_interval_ms 太多了，别叫 interval。
    // 也别和 config.ini 的 send_queue_capacity(=1024) 混为一谈 —— 那是队列条数上限，
    // 这个是日志节流，同为 1024 纯属巧合。
    //
    // 已知局限：计数是 per-session 的（network.h 的 queue_full_drops_ /
    // send_queue_full_drops_），所以日志总量仍随连接数线性增长。这个数只压得住单
    // 连接的输出，压不住全局 —— 真要保护 io 线程得改成全局聚合或时间窗口限流。
    inline constexpr uint32_t DROPS_PER_WARN_LOG = 1024;

}  // namespace cncpp

#endif  // CONSTDEFINE_H

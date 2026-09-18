// TimerWheel 行为验证程序
//
// 用固定周期在主线程驱动 TimerManager::tick(), 检查四条约定:
//   [1] 单次定时器: 触发时点落在 [delay, max(delay,一格) + 一格] 内, 即"不早于到期时间、最多晚一格"
//   [2] 重复定时器: 不随回调耗时累积漂移
//   [3] cancel: 能阻止后续触发, 且对重复定时器能阻止它重新入轮
//   [4] 回调内注册: 新定时器不会在本拍被执行, 也不会丢失
//
// 退出码 0 = 全部通过, 1 = 存在失败项

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "TimeUtils.h"
#include "timer_wheel.h"

using namespace cncpp;

namespace
{
    // 驱动周期, 必须等于传给 TimerManager::init() 的 tick_ms(见 ADR-0001)
    constexpr uint64_t kTickMs = 50;
    // 允许的额外抖动: 线程唤醒 + 同一拍内多个回调串行执行的耗时
    constexpr uint64_t kSlackMs = 40;
    // 整轮验证时长
    constexpr uint64_t kTotalMs = 3000;

    uint64_t g_t0     = 0;
    int      g_failed = 0;

    // 与 TimerWheel 内部同一个时钟, 否则量出来的误差没有意义
    uint64_t elapsed()
    {
        return cncpp::getNowMilliSecond() - g_t0;
    }

    void verdict(bool ok)
    {
        printf("  %s\n", ok ? "OK" : "FAIL");
        if (!ok)
            ++g_failed;
    }

    // ---------------------------------------------------------------- [1] 单次定时器

    struct OnceCase
    {
        uint64_t delay_ms = 0;
        uint64_t fired_ms = 0;
        int      count    = 0;
    };

    std::vector<OnceCase> g_once;

    void registerOnceCases()
    {
        const uint64_t delays[] = {0, 1, 25, 49, 50, 51, 99, 100, 250, 500, 501, 1000, 1500};
        for (uint64_t d : delays)
        {
            const size_t idx = g_once.size();
            g_once.push_back({d, 0, 0});
            sTimerManager.runAfter(d, [idx]() {
                OnceCase& c = g_once[idx];
                if (c.count == 0)
                    c.fired_ms = elapsed();
                ++c.count;
            });
        }
    }

    // 所有定时器都是在 t=0 注册的, 但要到第一拍结束时才被入站队列收割(见 ADR-0002), 所以
    //   delay < 一格 的定时器最早也只能在一格之后触发 -> 上界取 max(delay, 一格) + 一格
    uint64_t upperBound(uint64_t delay_ms)
    {
        const uint64_t base = delay_ms > kTickMs ? delay_ms : kTickMs;
        return base + kTickMs + kSlackMs;
    }

    void reportOnceCases()
    {
        printf("\n[1] 单次定时器: 触发时点必须落在 [delay, max(delay,一格) + 一格]\n");
        printf("    %-12s %16s %10s %6s\n", "delay", "expected", "actual", "");
        for (const auto& c : g_once)
        {
            const uint64_t upper = upperBound(c.delay_ms);
            const bool     ok    = (c.count == 1) && (c.fired_ms >= c.delay_ms) && (c.fired_ms <= upper);
            printf("    %-12llu [%4llu,%8llu] %10llu", static_cast<unsigned long long>(c.delay_ms),
                   static_cast<unsigned long long>(c.delay_ms), static_cast<unsigned long long>(upper),
                   static_cast<unsigned long long>(c.fired_ms));
            verdict(ok);
        }
    }

    // ---------------------------------------------------------------- [2] 重复定时器

    std::vector<uint64_t> g_repeat;

    void registerRepeatCase()
    {
        // 每 200ms 一次, 共 5 次
        sTimerManager.runEvery(200, []() {
            g_repeat.push_back(elapsed());
        }, 5);
    }

    void reportRepeatCase()
    {
        printf("\n[2] 重复定时器: 每 200ms 一次共 5 次, 检查次数与累积漂移\n");

        const bool count_ok = (g_repeat.size() == 5);
        printf("    触发次数 %zu (期望 5)", g_repeat.size());
        verdict(count_ok);

        for (size_t i = 0; i < g_repeat.size(); ++i)
        {
            const uint64_t expect = 200 * (static_cast<uint64_t>(i) + 1);
            const bool     ok     = (g_repeat[i] >= expect) && (g_repeat[i] <= expect + kTickMs + kSlackMs);
            printf("    第 %zu 次  期望 [%4llu,%4llu]  实际 %4llu", i + 1, static_cast<unsigned long long>(expect),
                   static_cast<unsigned long long>(expect + kTickMs + kSlackMs),
                   static_cast<unsigned long long>(g_repeat[i]));
            verdict(ok);
        }
    }

    // ---------------------------------------------------------------- [3] 取消

    std::vector<uint64_t> g_forever;
    uint64_t              g_forever_id = 0;

    void registerCancelCase()
    {
        g_forever_id = sTimerManager.runForever(200, []() {
            g_forever.push_back(elapsed());
        });

        // 1000ms 时取消, 之后不应再触发
        sTimerManager.runAfter(1000, [id = g_forever_id]() {
            sTimerManager.cancel(id);
        });
    }

    void reportCancelCase()
    {
        printf("\n[3] 取消: 永久定时器 200ms 一次, 1000ms 时取消\n");

        // 1000ms 时取消, 取消请求在下一拍生效, 所以最多还会在 1200ms 那一拍之前被拦住
        uint64_t last = 0;
        for (uint64_t t : g_forever)
            last = t > last ? t : last;

        const bool ok = !g_forever.empty() && (last <= 1000 + kSlackMs);
        printf("    最后触发 %4llu (期望 <= %llu), 共触发 %zu 次", static_cast<unsigned long long>(last),
               static_cast<unsigned long long>(1000 + kSlackMs), g_forever.size());
        verdict(ok);
    }

    // ---------------------------------------------------------------- [4] 回调内注册

    std::vector<uint64_t> g_chain;

    void registerChainCase()
    {
        // 第一环 300ms; 之后每一环的回调里再注册下一环, 延迟 0
        sTimerManager.runAfter(300, []() {
            g_chain.push_back(elapsed());
            sTimerManager.runAfter(0, []() {
                g_chain.push_back(elapsed());
                sTimerManager.runAfter(0, []() {
                    g_chain.push_back(elapsed());
                });
            });
        });
    }

    void reportChainCase()
    {
        printf("\n[4] 回调内注册: 三环必须各触发一次, 且逐环变晚(不能在本拍被执行)\n");

        const bool count_ok = (g_chain.size() == 3);
        printf("    触发次数 %zu (期望 3)", g_chain.size());
        verdict(count_ok);

        if (g_chain.size() < 2)
            return;

        for (size_t i = 1; i < g_chain.size(); ++i)
        {
            // 差值 > 0 是关键: 为 0 说明新定时器被塞进了正在遍历的那个槽, 本拍就执行了
            const uint64_t diff = g_chain[i] - g_chain[i - 1];
            const bool     ok   = (diff > 0) && (diff <= kTickMs + kSlackMs);
            printf("    第 %zu 环 -> 第 %zu 环 间隔 %4llu (期望 (0, %llu])", i, i + 1,
                   static_cast<unsigned long long>(diff), static_cast<unsigned long long>(kTickMs + kSlackMs));
            verdict(ok);
        }
    }

    // ---------------------------------------------------------------- 驱动

    void drive()
    {
        const auto start = std::chrono::steady_clock::now();
        const int  ticks = static_cast<int>(kTotalMs / kTickMs);
        for (int i = 1; i <= ticks; ++i)
        {
            std::this_thread::sleep_until(start + std::chrono::milliseconds(i * kTickMs));
            sTimerManager.tick();
        }
    }
}  // namespace

int main()
{
    printf("TimerWheel 行为验证: 一格 = %llu ms, 驱动 %llu ms (%llu 拍)\n", static_cast<unsigned long long>(kTickMs),
           static_cast<unsigned long long>(kTotalMs), static_cast<unsigned long long>(kTotalMs / kTickMs));

    // 一格 = 主循环周期, 见 ADR-0001
    sTimerManager.init(kTickMs);
    g_t0 = cncpp::getNowMilliSecond();

    registerOnceCases();
    registerRepeatCase();
    registerCancelCase();
    registerChainCase();

    drive();

    reportOnceCases();
    reportRepeatCase();
    reportCancelCase();
    reportChainCase();

    sTimerManager.stop();

    printf("\n%s (失败项 %d)\n", g_failed == 0 ? "全部通过" : "存在失败项", g_failed);
    return g_failed == 0 ? 0 : 1;
}

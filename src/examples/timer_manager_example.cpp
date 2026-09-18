#include <iostream>
#include <thread>
#include "Global.h"
#include "config.h"
#include "io_context_pool.h"
#include "logger.h"
#include "timer_wheel.h"

using namespace cncpp;

// 测试单次定时器
void testSingleTimer()
{
    LOG_INFO("=== 测试单次定时器 ===");

    // 测试 runAfter - 100ms 后执行
    sTimerManager.runAfter(100, []() {
        LOG_INFO("单次定时器执行: 100ms 后");
    });

    // 测试 runAt - 绝对时间执行
    // 基准必须是 getNowMilliSecond(), 与 TimerWheel::addTimer 内部一致;
    // getProcessTickMs() 是进程启动时刻的快照, 用它会让定时器一进来就"已过期"
    uint64_t futureTime = cncpp::getNowMilliSecond() + 200;
    sTimerManager.runAt(futureTime, []() {
        LOG_INFO("单次定时器执行: 200ms 后（绝对时间）");
    });
}

// 测试重复定时器
void testRepeatedTimer()
{
    LOG_INFO("=== 测试重复定时器 ===");

    // 测试 runEvery - 每 100ms 执行一次，共执行 5 次
    sTimerManager.runEvery(100, []() {
        static int count = 0;
        LOG_INFO("重复定时器执行: 第 {} 次", ++count);
    }, 5);
}

// 测试永久定时器
void testForeverTimer()
{
    LOG_INFO("=== 测试永久定时器 ===");

    // 测试 runForever - 每 150ms 执行一次，直到程序结束
    sTimerManager.runForever(150, []() {
        static int count = 0;
        LOG_INFO("永久定时器执行: 第 {} 次", ++count);
    });
}

// 测试定时器取消
void testTimerCancel()
{
    LOG_INFO("=== 测试定时器取消 ===");

    // 永久定时器, 1s 后取消, 之后不应再触发
    uint64_t id = sTimerManager.runForever(200, []() {
        LOG_INFO("这个定时器应该在 1s 后被取消");
    });
    LOG_INFO("注册永久定时器 id={}", id);

    sTimerManager.runAfter(1000, [id]() {
        LOG_INFO("取消定时器 id={}", id);
        sTimerManager.cancel(id);
    });
}

int main(int argc, char* argv[])
{
    // 读取配置文件
    if (!sConfig.ReadFromCommandLine(argc, argv))
    {
        return 1;
    }

    // 初始化日志系统
    sLogger.init({});

    // 初始化 IO 上下文池
    if (!sIOContextPool.init())
    {
        LOG_ERROR("Failed to initialize IO context pool");
        return 1;
    }

    // 初始化定时器管理器, 一格 = 主循环周期, 见 ADR-0001
    sTimerManager.init(sMainConfig.main_loop_interval_ms());

    // 运行测试
    testSingleTimer();
    testTimerCancel();
    // testRepeatedTimer();
    // testForeverTimer();

    sIOContextPool.setTimerCallback([]() {
        sTimerManager.tick();
    }, sMainConfig.main_loop_interval_ms());

    sIOContextPool.run();
    LOG_INFO("测试完成，退出程序");

    sTimerManager.stop();

    // 停止 IO 上下文池
    sIOContextPool.stop();
    sIOContextPool.waitForStop();

    return 0;
}

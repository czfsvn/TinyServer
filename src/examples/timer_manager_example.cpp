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
    uint64_t futureTime = cncpp::getProcessTickMs() + 200;
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

    // 这里可以扩展测试定时器的取消功能
    // 注意：当前 TimerManager 还没有提供取消定时器的接口
    LOG_INFO("定时器取消测试：待实现");
}

int main(int argc, char* argv[])
{
    // 读取配置文件
    if (!sConfig.ReadFromCommandLine(argc, argv))
    {
        return 1;
    }

    // 初始化日志系统
    sLogger.init();

    // 初始化 IO 上下文池
    if (!sIOContextPool.init())
    {
        LOG_ERROR("Failed to initialize IO context pool");
        return 1;
    }

    // 初始化定时器管理器
    sTimerManager.init();

    // 运行测试
    testSingleTimer();
    // testRepeatedTimer();
    // testForeverTimer();
    // testTimerCancel();

    sIOContextPool.setTimerCallback([]() {
        sTimerManager.tick();
    }, sMainConfig.asio_timer_interval_ms());

    sIOContextPool.run();
    // 停止定时器线程（当前版本没有 stop 方法，这里直接退出）
    LOG_INFO("测试完成，退出程序");

    // 停止 IO 上下文池
    sIOContextPool.stop();
    sIOContextPool.waitForStop();

    return 0;
}

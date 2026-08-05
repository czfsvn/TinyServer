#include "service.h"
#include "TimeUtils.h"
#include "crash_handler.h"
#include "io_context_pool.h"
#include "logger.h"
#include "timer_wheel.h"

namespace cncpp
{
    Service::Service()
    {
        std::cout << "Service constructor" << std::endl;
    }

    Service::~Service()
    {
        std::cout << "Service destructor" << std::endl;
        if (is_running_.load())
        {
            stop();
        }
        else
        {
            // 安全网：如果 stop() 未被调用，在此清理
            // 顺序：signal_handler 先于 IOContextPool，因为 signal_set 持有 main_io_context 的引用
            // cleanup() 内部有幂等保护，重复调用安全
            signal_handler_.cleanup();
            sIOContextPool.cleanup();
        }
    }

    bool Service::run(int argc, char* argv[])
    {
        std::cout << "Initializing Service..." << std::endl;

        // 初始化配置
        if (!sConfig.ReadFromCommandLine(argc, argv))
        {
            std::cerr << "Failed to read config from command line args" << std::endl;
            return false;
        }

        // 初始化日志
        if (!sLogger.init(sMainConfig.log_yml_path()))
        {
            std::cerr << "Failed to init logger" << std::endl;
            return false;
        }

        // 初始化 IOContextPool
        if (!sIOContextPool.init())
        {
            LOG_ERROR("Failed to init IOContextPool");
            sLogger.shutdown();
            return false;
        }

        // 加载游戏配置
        loadGameConfigs();

        // 调用派生类初始化
        if (!onInit() || !start())
        {
            LOG_ERROR("Derived class init failed");
            signal_handler_.cleanup();
            sIOContextPool.cleanup();
            sLogger.shutdown();
            return false;
        }

        LOG_DEBUG("[Service] run end");

        // start() 返回意味着 sIOContextPool.run() 已退出（信号回调已调 stop()）
        // 直接执行完整清理
        waitForStop();

        return true;
    }

    bool Service::start()
    {
        LOG_INFO("Starting Service...");

        // 使用 IOContextPool 的 main_io_context 注册信号处理
        // 这样信号回调会作为 asio handler 运行在 main_io_context 的线程上（即主线程），
        // 回调里调 sIOContextPool.stop() 可以让 main_io_context_->run() 返回，
        // 从而让 start() → run() 返回，run() 内部自动调 waitForStop() 完成清理。
        if (!signal_handler_.init(sIOContextPool.getMainIOContext()))
        {
            LOG_ERROR("Failed to init signal handler");
            return false;
        }
        // 初始化崩溃处理（崩溃信号用平台原生同步机制，不走 io_context）
        CrashHandler::init();

        // 信号回调：调用 sIOContextPool.stop() 停止所有 io_context
        // main_io_context_->run() 会在此回调返回后退出，
        // 从而 start() → run() 返回，run() 内部自动调 waitForStop() 完成清理
        signal_handler_.setGracefulShutdownCallback([]() {
            LOG_INFO("Graceful shutdown requested via signal");
            sIOContextPool.stop();
        });

        // 调用派生类启动
        if (!onStart())
        {
            LOG_ERROR("Derived class start failed");
            LOG_INFO("Cleaning up after failed start...");
            is_running_.store(false);
            onStop();
            return false;
        }

        sTimerManager.init();
        sIOContextPool.setTimerCallback(std::bind(&Service::tick, this), getMainLoopIntervalMs());

        is_running_.store(true);
        sIOContextPool.run();  // 阻塞在 main_io_context_->run()，信号回调会调 stop() 使其返回

        return true;
    }

    void Service::stop()
    {
        LOG_INFO("Stopping Service...");

        if (!is_running_.exchange(false))
        {
            LOG_WARN("Service is not running");
            return;
        }

        // 1. 派生类停止（可以写日志）
        onStop();

        // 2. 停止定时器
        sTimerManager.stop();

        LOG_INFO("Service stopped successfully");

        // 3. 清理信号处理器（必须在 IOContextPool 之前！）
        //    signal_set 持有 main_io_context 的引用，如果 main_io_context 先被销毁，
        //    signal_set_->cancel() / reset() 会访问已释放内存 → UB
        signal_handler_.cleanup();

        // 4. 清理 IOContextPool（stop io_context → join 线程 → requestShutdown logger → clear）
        sIOContextPool.cleanup();
    }

    void Service::waitForStop()
    {
        LOG_DEBUG("[Service] waitForStop start");

        // 执行全部清理（onStop → timer → signal_handler → IOContextPool）
        stop();
        // 在 main() 返回前彻底关闭 logger（必须在全局析构前完成）
        sLogger.shutdown();

        std::cout << "[Service] waitForStop end" << std::endl;
    }

    uint64_t Service::getMainLoopIntervalMs() const
    {
        return sMainConfig.main_loop_interval_ms();
    }

    void Service::tick()
    {
        steady_clock::time_point now   = steady_clock::now();
        uint64_t                 delta = cncpp::getMsDiff(curr_tick_time_, now);
        if (delta < getMainLoopIntervalMs())
            return;

        curr_tick_time_ = now;
        sTimerManager.tick();
        onTick();
    }

}  // namespace cncpp

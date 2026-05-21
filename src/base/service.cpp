#include "service.h"
#include "TimeUtils.h"
#include "io_context_pool.h"
#include "logger.h"
#include "timer_wheel.h"

namespace cncpp
{
    Service::Service()
    {
        LOG_DEBUG("Service constructor");
    }

    Service::~Service()
    {
        LOG_DEBUG("Service destructor");
        if (is_running_.load())
        {
            stop();
        }
    }

    bool Service::init(int argc, char* argv[])
    {
        std::cout << "Initializing Service..." << std::endl;

        // 初始化配置
        if (!sConfig.ReadFromCommandLine(argc, argv))
        {
            std::cerr << "Failed to read config from command line args" << std::endl;
            return false;
        }

        // 初始化日志
        if (!sLogger.init())
        {
            std::cerr << "Failed to init logger" << std::endl;
            return false;
        }

        // 初始化 IOContextPool
        if (!sIOContextPool.init())
        {
            LOG_ERROR("Failed to init IOContextPool");
            return false;
        }

        // 调用派生类初始化
        if (!onInit())
        {
            LOG_ERROR("Derived class init failed");
            return false;
        }

        sTimerManager.init();

        LOG_INFO("Service initialized successfully");
        return true;
    }

    bool Service::start()
    {
        LOG_INFO("Starting Service...");
        sIOContextPool.setGracefulShutdownCallback([this]() {
            std::cout << "Starting graceful shutdown..." << std::endl;
            this->stop();
        });

        sIOContextPool.enableCoreDump();

        // 调用派生类启动
        if (!onStart())
        {
            LOG_ERROR("Derived class start failed");
            return false;
        }

        sIOContextPool.setTimerCallback(std::bind(&Service::tick, this), getMainLoopIntervalMs());

        is_running_.store(true);
        sIOContextPool.run();

        LOG_INFO("Service started successfully");
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

        // 调用派生类停止
        onStop();

        // 等待一小段时间，让正在进行的异步操作完成
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 第三步：停止 IO 上下文池（停止所有 io_context 和工作线程）
        sIOContextPool.stop();

        // 等待工作线程退出（带超时）
        sIOContextPool.waitForStopWithTimeout(std::chrono::seconds(10));

        sIOContextPool.cleanup();

        // 停止 IOContextPool
        sIOContextPool.stop();
        sIOContextPool.waitForStop();

        LOG_INFO("Service stopped successfully");

        sLogger.shutdown();

        is_running_ = false;
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
        sTimerManager.timer_update();
        onTick();
    }

}  // namespace cncpp
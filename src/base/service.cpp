#include "service.h"
#include "TimeUtils.h"
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
            // 即使没有运行，也需要清理 IOContextPool（可能初始化了但未运行）
            // cleanup() 内部有幂等保护，重复调用安全
            sIOContextPool.cleanup();
        }
        // 注意：不在这里调用 sLogger.shutdown()。
        // sLogger.shutdown() 内部会调用 spdlog::shutdown()，访问 spdlog 的全局
        // registry 静态对象。此处处于全局析构阶段，spdlog 的 registry 可能已经
        // 被析构（跨 TU 析构顺序未定义），调用 spdlog::shutdown() 会访问已释放
        // 的内存 → UB。
        // 正确做法是由 main() 在返回前调用 sLogger.shutdown()，此时全局析构尚未
        // 开始，调用顺序确定。shutdown() 本身有幂等保护，main() 调过后再来这里
        // 也是空操作。若 main() 未能调用（异常退出），则由 OS 回收资源。
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
            return false;
        }

        // 加载游戏配置
        loadGameConfigs();

        // 调用派生类初始化
        if (!onInit())
        {
            LOG_ERROR("Derived class init failed");
            return false;
        }

        sTimerManager.init();

        // 注意：必须把 start() 的返回值原样返回。
        // 之前这里直接 return true，导致 onStart() 失败时 main() 以为启动成功，
        // 后续析构序列进入和设计不同的状态，是导致退出段错误的关键一环。
        return start();
    }

    bool Service::start()
    {
        LOG_INFO("Starting Service...");
        sIOContextPool.setGracefulShutdownCallback([this]() {
            // 信号线程只设标志 + 唤醒主线程，不调 stop()。
            // 这样所有清理工作（onStop、cleanup、signal_handler_.cleanup）
            // 都在主线程上执行，避免信号线程自毁 io_context 的 UB。
            LOG_INFO("Graceful shutdown requested via signal");
            {
                // 持锁修改共享状态，防止 wait() 谓词检查与 notify 之间的竞态
                std::lock_guard<std::mutex> lock(stop_mutex_);
                shutdown_requested_.store(true);
            }
            stop_cv_.notify_all();
        });

        sIOContextPool.enableCoreDump();

        // 调用派生类启动
        if (!onStart())
        {
            LOG_ERROR("Derived class start failed");
            // 启动失败：让派生类先清理自己的资源（onStop），
            // 然后标记 is_running_ 让析构函数去统一清理 IOContextPool。
            // 不要在这里直接 cleanup，否则会过早释放 io_context，
            // 导致全局成员（如 acceptor_/tiny_client_/data_client_）析构时
            // 访问已销毁的 io_context 而崩溃。
            LOG_INFO("Cleaning up after failed start...");
            is_running_.store(false);
            onStop();
            return false;
        }

        sIOContextPool.setTimerCallback(std::bind(&Service::tick, this), getMainLoopIntervalMs());

        is_running_.store(true);
        sIOContextPool.run();
        // stop();

        // LOG_INFO("Service started successfully");
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

        // 调用派生类停止（在 requestShutdown 之前）
        onStop();

        // 先停止定时器（在 requestShutdown 之前）
        sTimerManager.stop();

        // 不再使用 sleep_for(100ms) 等待异步操作完成。
        // onStop() 中的 disconnect/reset 会同步关闭 socket，
        // 后续 cleanup() 会 stop io_context 并 join 线程，
        // 已排队的事件处理器会在 io_context::stop() 后执行完毕。
        LOG_INFO("Service stopped successfully");

        // 清理 IOContextPool（内部会调用 requestShutdown，此后 LOG_ 宏返回 nullptr）
        sIOContextPool.cleanup();
    }

    void Service::wait()
    {
        std::unique_lock<std::mutex> lock(stop_mutex_);
        stop_cv_.wait(lock, [this]() {
            return shutdown_requested_.load();
        });
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
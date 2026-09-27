#include "service.h"
#include "crash_handler.h"
#include "io_context_pool.h"
#include "logger.h"
#include "timer_wheel.h"
#include "TimeUtils.h"

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
            // 仍在运行：走完整停止流程（业务钩子 onStop + 资源清理）
            stop();
        }
        else
        {
            // 安全网：stop() 没走过、或只走了一半时，在此兜底清理资源
            // cleanupResources() 每个步骤都幂等，重复调用安全
            cleanupResources();
        }
    }

    bool Service::run(int argc, char* argv[])
    {
        // 记录主循环线程：stop() 的完整流程会 join IOContextPool 的 worker 线程，
        // 必须在同一线程执行（见 stop()）。
        // 必须在 IOContextPool::init() 创建 worker 线程之前写：
        // std::thread 的构造建立 happens-before，worker 线程才能安全地读到这个值。
        main_thread_id_ = std::this_thread::get_id();

        // 崩溃处理要尽早装上：ReadFromCommandLine / Logger::init / IOContextPool::init /
        // loadGameConfigs 这一段恰恰最容易崩，装在 start() 里就太晚了。
        // 此刻尚无任何资源需要清理，故放在 try 之外（init 自身失败也无资源可回溯）。
        CrashHandler::init();

        // 异常边界：派生类钩子（loadGameConfigs/onInit/onStart）抛出的异常必须在这里收住。
        // 否则会穿出 main() 直接 terminate，清理路径一行都执行不到（日志也来不及 flush）。
        try
        {
            return runImpl(argc, argv);
        }
        catch (const std::exception& e)
        {
            // 先 cerr 再 LOG：异常可能发生在 sLogger.init() 之前，此时 LOG_* 是空操作
            std::cerr << "[Service] run() exception: " << e.what() << std::endl;
            LOG_ERROR("[Service] run() exception: {}", e.what());
            cleanupResources();
            return false;
        }
        catch (...)
        {
            std::cerr << "[Service] run() unknown exception" << std::endl;
            LOG_ERROR("[Service] run() unknown exception");
            cleanupResources();
            return false;
        }
    }

    bool Service::runImpl(int argc, char* argv[])
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
            cleanupResources();
            return false;
        }

        // 初始化时间轮：一格 = 主循环周期, 见 ADR-0001
        // 必须在 loadGameConfigs()/onInit()/onStart() 之前：
        // TimerManager::init() 是"整体重置"语义，放在其后调用会清空派生类已注册的定时器
        sTimerManager.init(getMainLoopIntervalMs());

        // 加载游戏配置
        loadGameConfigs();

        // 调用派生类初始化
        if (!onInit() || !start())
        {
            LOG_ERROR("Derived class init failed");
            cleanupResources();
            return false;
        }

        LOG_DEBUG("[Service] run end");

        // start() 返回意味着 sIOContextPool.run() 已退出（信号回调已调 stop()）
        // 直接执行完整清理
        waitForStop();

        return true;
    }

    void Service::cleanupResources() noexcept
    {
        // 唯一的资源清理出口：正常停止（stop）、启动失败、异常兜底、析构兜底都走这里。
        // 顺序敏感：
        // - signal_handler 持有 main_io_context 的引用，必须先于 IOContextPool 销毁
        // - sLogger.shutdown() 放最后，保证前面几步还能写日志
        // 每一步都单独吞异常：本函数会在 catch 块和析构函数中被调用，绝不能再抛
        try
        {
            sTimerManager.stop();
        }
        catch (...)
        {
        }

        try
        {
            signal_handler_.cleanup();
        }
        catch (...)
        {
        }

        try
        {
            sIOContextPool.cleanup();
        }
        catch (...)
        {
        }

        try
        {
            sLogger.shutdown();
        }
        catch (...)
        {
        }
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
        // 崩溃处理已在 run() 开头安装（CrashHandler::init），此处不再重复

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
            // onStart() 现在可以合法注册定时器（TimerManager 已提前 init），失败时要一并停掉
            sTimerManager.stop();
            return false;
        }

        // 时间轮已在 run() 中 init（必须先于 onInit/onStart）
        sIOContextPool.setTimerCallback(std::bind(&Service::tick, this), getMainLoopIntervalMs());

        is_running_.store(true);
        sIOContextPool.run();  // 阻塞在 main_io_context_->run()，信号回调会调 stop() 使其返回

        return true;
    }

    void Service::stop()
    {
        LOG_INFO("Stopping Service...");

        // 非主线程（例如 worker 线程的 handler 里）不能走完整流程：
        // cleanupResources() 会 join worker 线程，在这里 join 自己会抛 std::system_error，
        // 清理会烂在半路。只请求停止，让主循环返回，onStop() + 清理交给主线程。
        if (main_thread_id_ != std::thread::id{} && main_thread_id_ != std::this_thread::get_id())
        {
            LOG_WARN("[Service] stop() from non-main thread: requesting stop only, "
                     "onStop() and cleanup will run on the main thread");
            sIOContextPool.stop();
            return;
        }

        if (!is_running_.exchange(false))
        {
            LOG_WARN("Service is not running");
            return;
        }

        // 1. 派生类停止（此时日志仍可用）
        onStop();

        LOG_INFO("Service stopped successfully");

        // 2. 收尾清理（定时器 → 信号处理器 → IOContextPool → logger），
        //    顺序约束与幂等保护都收敛在 cleanupResources() 内
        cleanupResources();
    }

    void Service::waitForStop()
    {
        LOG_DEBUG("[Service] waitForStop start");

        // 执行全部清理（onStop → timer → signal_handler → IOContextPool → logger）
        // sLogger.shutdown() 已收进 stop()，任何调用 stop() 的路径都能关掉日志，
        // 不再依赖"必须走 waitForStop()"
        stop();

        std::cout << "[Service] waitForStop end" << std::endl;
    }

    uint64_t Service::getNowMs() const
    {
        // 墙钟毫秒，与 cncpp::getNowMilliSecond() 同一基准（不再是 steady_clock 的开机毫秒）。
        // 直接取 IOContextPool 每拍刷新好的 tick 时间戳：
        // 时间戳只在一处产生，业务与定时器看到的相位完全一致
        return sIOContextPool.getCurrentTickMs();
    }

    uint64_t Service::getMainLoopIntervalMs() const
    {
        // 取 IOContextPool 实际生效的间隔，而不是配置原值：
        // 配置为 0 时 IOContextPool 会回退到默认值，这里若仍返回 0，
        // 时间轮一格就不再等于主循环周期，违反 ADR-0001 的前提。
        return sIOContextPool.getIntervalMs();
    }

    void Service::tick()
    {
        // 节流统一收口在 IOContextPool（setTimerCallback 传入的 interval_ms），此处不再二次判断。
        // 原来这里还有一层 delta < interval：两层节流的时间源（steady_clock 纳秒 vs 墙钟毫秒）
        // 与相位（curr_tick_time_ vs last_call_back_）各自维护，一旦不一致就静默丢拍且无日志。
        sTimerManager.tick();
        onTick();
    }

}  // namespace cncpp

/**
 * @brief 主服务基类
 * 
 * 封装通用的服务组件，提供统一的初始化、启动、停止流程。
 * 包含配置管理、日志记录、IOContextPool、信号处理等功能。
 */
#pragma once

#include <atomic>
#include <chrono>
#include <thread>

#include "signal_handler.h"

namespace cncpp
{
    class Service
    {
    public:
        /**
     * @brief 构造函数
     */
        Service();

        /**
     * @brief 析构函数
     */
        virtual ~Service();

        /**
     * @brief 禁用拷贝构造
     */
        Service(const Service&) = delete;

        /**
     * @brief 禁用赋值运算符
     */
        Service& operator=(const Service&) = delete;

        /**
     * @brief 初始化服务
     * @return 是否成功
     */
        bool run(int argc, char* argv[]);

        /**
     * @brief 停止服务
     *
     * 线程约束：完整停止流程必须从主线程（即调用 run() 的线程）执行 ——
     * 它会 join IOContextPool 的 worker 线程。
     * 从 worker 线程调用只会"请求停止"（让主循环返回），
     * 真正的 onStop() + 资源清理留给主线程的 waitForStop()；
     * 这样避免了 worker 线程 join 自己抛 std::system_error、清理烂在半路。
     */
        void stop();

        /**
     * @brief 检查服务是否运行中
     * @return 是否运行中
     */
        bool isRunning() const
        {
            return is_running_.load();
        }

        /**
     * @brief 获取服务运行时间（毫秒）
     * @return 服务运行时间（毫秒）
     */
        uint64_t getUptimeMs() const
        {
            const auto elapsed = std::chrono::steady_clock::now() - app_start_time_;
            return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        }

        /**
     * @brief 获取当前 tick 时间戳（墙钟毫秒，与 cncpp::getNowMilliSecond() 同一基准）
     * @return 当前时间（毫秒）
     */
        uint64_t getNowMs() const;

        /**
     * @brief 获取主循环间隔（毫秒）
     * @return 主循环间隔（毫秒）
     */
        uint64_t getMainLoopIntervalMs() const;

    protected:
        /**
     * @brief 派生类初始化钩子
     * @return 是否成功
     */
        virtual bool onInit()
        {
            return true;
        }

        /**
     * @brief 派生类启动钩子
     * @return 是否成功
     */
        virtual bool onStart()
        {
            return true;
        }

        /**
     * @brief 派生类停止钩子
     */
        virtual void onStop()
        {
        }

        virtual void loadGameConfigs()
        {
        }
        /**
     * @brief 派生类主循环钩子
     *
     * 无返回值：调用方（Service::tick）不会依据它做任何事，给 bool 只会误导派生类
     * 以为返回 false 有意义。需要在 tick 内请求停服时，直接调用 stop() 即可
     * （tick 跑在主线程，满足 stop() 的线程约束）。
     */
        virtual void onTick()
        {
        }

    private:
        /**
     * @brief 执行完整清理（stop + sLogger.shutdown）
     * run() 在 sIOContextPool.run() 返回后自动调用
     */
        void waitForStop();

        /**
     * @brief run() 的实际流程，异常统一由 run() 兜底
     */
        bool runImpl(int argc, char* argv[]);

        /**
     * @brief 启动服务（仅供 runImpl() 内部调用，不是对外接口）
     *
     * private 的原因：它依赖 runImpl() 已完成的前置步骤（config → logger →
     * IOContextPool → TimerManager），单独调用会崩在 getMainIOContext()。
     * 对外唯一入口是 run()。
     */
        bool start();

        /**
     * @brief 服务主循环（由 IOContextPool 的定时器回调每拍驱动一次）
     *
     * private 的原因：时间戳只在 IOContextPool 那一处产生，业务与定时器看到的
     * 相位才一致；外部手动调会打乱时间轮相位。
     */
        void tick();

        /**
     * @brief 统一的资源清理路径（定时器 → 信号处理器 → IOContextPool → logger）
     *
     * 不含业务钩子 onStop()：启动失败/异常场景下不该回调派生类。
     * noexcept：会在 catch 块与析构函数中被调用，内部逐步骤吞掉异常，绝不再抛。
     * 每个步骤自身幂等，重复调用安全。
     */
        void cleanupResources() noexcept;

        std::atomic<bool> is_running_{false};

        // 跑主循环的线程（start() 的调用线程），stop() 必须在同一线程执行，见 stop() 说明
        std::thread::id main_thread_id_{};

        // 信号处理器（直接持有，不再通过 IOContextPool 转发）
        SignalHandler signal_handler_;

        // 服务启动时间（getUptimeMs 用）
        std::chrono::steady_clock::time_point app_start_time_ = std::chrono::steady_clock::now();
    };

}  // namespace cncpp

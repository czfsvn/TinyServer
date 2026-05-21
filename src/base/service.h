/**
 * @brief 主服务基类
 * 
 * 封装通用的服务组件，提供统一的初始化、启动、停止流程。
 * 包含配置管理、日志记录、IOContextPool、信号处理等功能。
 */
#pragma once

#include <atomic>
#include <chrono>

using namespace std::chrono;

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
        bool init(int argc, char* argv[]);

        /**
     * @brief 启动服务
     * @return 是否成功
     */
        bool start();

        /**
     * @brief 停止服务
     */
        void stop();

        /**
     * @brief 等待服务停止
     */
        void wait();

        /**
     * @brief 检查服务是否运行中
     * @return 是否运行中
     */
        bool isRunning() const
        {
            return is_running_.load();
        }

        /**
     * @brief 服务主循环
     */
        void tick();

        uint64_t getUptimeMs() const
        {
            return duration_cast<milliseconds>(std::chrono::steady_clock::now() - app_start_time_).count();
        }

        uint64_t getNowMs() const
        {
            return duration_cast<milliseconds>(curr_tick_time_.time_since_epoch()).count();
        }

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

        /**
     * @brief 派生类主循环钩子
     * @return 是否成功
     */
        virtual bool onTick()
        {
            return true;
        }

    private:
        std::atomic<bool> is_running_{false};

        // 服务启动时间
        std::chrono::steady_clock::time_point app_start_time_ = std::chrono::steady_clock::now();
        // 当前时间
        std::chrono::steady_clock::time_point curr_tick_time_ = app_start_time_;
    };

}  // namespace cncpp

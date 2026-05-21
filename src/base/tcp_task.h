#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include "network.h"

namespace cncpp
{

    /**
 * @brief TCP任务状态枚举
 */
    enum class TcpTaskState
    {
        Idle,            // 空闲
        Connecting,      // 连接中
        Connected,       // 已连接
        Authenticating,  // 认证中
        Ready,           // 就绪（可处理消息）
        Processing,      // 处理中
        Disconnecting,   // 断开中
        Disconnected     // 已断开
    };

    /**
 * @brief TCP任务组件类
 * 
 * 提供通用的 TCP 连接任务功能，支持继承和组合使用。
 * 设计特点：
 * 1. 使用虚函数提供钩子，方便子类扩展
 * 2. 线程安全的状态管理
 * 3. 自动ID生成
 * 4. 连接时间和活跃时间跟踪
 */
    class TcpTask : public std::enable_shared_from_this<TcpTask>
    {
    public:
        TcpTask(tcp::socket&& socket);
        virtual ~TcpTask();

        // 禁止拷贝和移动
        TcpTask(const TcpTask&)            = delete;
        TcpTask& operator=(const TcpTask&) = delete;
        TcpTask(TcpTask&&)                 = delete;
        TcpTask& operator=(TcpTask&&)      = delete;

        /**
     * @brief 启动任务
     */
        void start();

        /**
     * @brief 停止任务
     */
        void stop();

        /**
     * @brief 获取任务ID
     * @return 任务唯一标识
     */
        uint32_t getTaskID() const
        {
            return task_id_;
        }

        /**
     * @brief 获取当前状态
     * @return 任务状态
     */
        TcpTaskState getState() const;

        /**
     * @brief 获取会话对象
     * @return 会话指针
     */
        std::shared_ptr<Session> getSession() const
        {
            return session_;
        }

        /**
     * @brief 获取客户端IP地址
     * @return 客户端IP
     */
        const std::string& getClientIP() const
        {
            return client_ip_;
        }

        /**
     * @brief 获取连接时间
     * @return 连接时间点
     */
        std::chrono::steady_clock::time_point getConnectTime() const
        {
            return connect_time_;
        }

        /**
     * @brief 获取最后活跃时间
     * @return 最后活跃时间点
     */
        std::chrono::steady_clock::time_point getLastActiveTime() const
        {
            return last_active_time_;
        }

        /**
     * @brief 发送消息
     * @param message 网络消息
     * @return 是否发送成功
     */
        bool sendMessage(const NetworkMessage& message);

        /**
     * @brief 判断是否活跃
     * @return 是否活跃
     */
        bool isActive() const;

        /**
     * @brief 更新最后活跃时间
     */
        void updateLastActiveTime();

        /**
     * @brief 设置消息回调
     * @param callback 消息回调函数
     */
        void setMessageCallback(std::function<void(const NetworkMessage&)> callback);

        /**
     * @brief 设置断开回调
     * @param callback 断开回调函数
     */
        void setCloseCallback(std::function<void()> callback);

    protected:
        /**
     * @brief 处理连接成功（钩子方法，子类可重写）
     */
        virtual void onConnected();

        /**
     * @brief 处理消息（钩子方法，子类可重写）
     * @param message 接收到的消息
     */
        virtual void onMessage(const cncpp::NetworkMessage& message);

        /**
     * @brief 处理断开（钩子方法，子类可重写）
     */
        virtual void onDisconnected();

        /**
     * @brief 更新任务状态
     * @param new_state 新状态
     */
        void setState(TcpTaskState new_state);

        /**
     * @brief 获取状态字符串描述
     * @param state 状态枚举
     * @return 状态描述字符串
     */
        static std::string stateToString(TcpTaskState state);

    protected:
        std::shared_ptr<Session>              session_;           // 网络会话
        std::string                           client_ip_;         // 客户端IP地址
        std::chrono::steady_clock::time_point connect_time_;      // 连接时间
        std::chrono::steady_clock::time_point last_active_time_;  // 最后活跃时间

    private:
        /**
     * @brief 内部消息处理
     */
        void handleMessage(const cncpp::NetworkMessage& message);

        /**
     * @brief 内部断开处理
     */
        void handleClose();

    private:
        static std::atomic<uint32_t> task_id_counter_;  // 任务ID计数器

        uint32_t                  task_id_;                    // 任务唯一标识
        std::atomic<TcpTaskState> state_{TcpTaskState::Idle};  // 任务状态
        std::mutex                state_mutex_;                // 状态保护锁

        std::function<void(const cncpp::NetworkMessage&)> msg_callback_;    // 消息回调
        std::function<void()>                             close_callback_;  // 断开回调
    };

    using TcpTaskPtr = std::shared_ptr<TcpTask>;

}  // namespace cncpp
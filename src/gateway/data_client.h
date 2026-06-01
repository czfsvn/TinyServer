#pragma once

#include <atomic>
#include <memory>
#include <string>
#include "tcp_client.h"

/**
 * @brief DataClient类，用于与DataServer建立连接并通信
 *
 * 继承自TcpClient，提供与数据服务器通信的特定功能。
 */
class DataClient : public cncpp::TcpClient
{
public:
    /**
     * @brief 构造函数
     */
    DataClient();

    /**
     * @brief 析构函数
     */
    ~DataClient() override;

    // 禁止拷贝和移动
    DataClient(const DataClient&)            = delete;
    DataClient& operator=(const DataClient&) = delete;
    DataClient(DataClient&&)                 = delete;
    DataClient& operator=(DataClient&&)      = delete;

    /**
     * @brief 发送ping请求检测连接状态
     * @return 是否发送成功
     */
    bool sendPing();

    /**
     * @brief 获取最后ping时间
     * @return 最后ping时间戳
     */
    uint64_t getLastPingTime() const
    {
        return last_ping_time_;
    }

protected:
    /**
     * @brief 处理连接成功（重写父类钩子）
     */
    void onConnectSuccess() override;

    /**
     * @brief 处理连接失败（重写父类钩子）
     * @param error_msg 错误信息
     */
    void onConnectError(const std::string& error_msg) override;

    /**
     * @brief 处理消息接收（重写父类钩子）
     * @param message 接收到的消息
     */
    void onMessageReceived(const cncpp::NetworkMessage& message) override;

    /**
     * @brief 处理连接断开（重写父类钩子）
     */
    void onDisconnected() override;

private:
    /**
     * @brief 发送ping响应
     * @param message 消息内容
     */
    void handlePingResponse(const cncpp::NetworkMessage& message);

private:
    std::atomic<uint64_t> last_ping_time_{0};  // 最后ping时间戳
};

using DataClientPtr = std::shared_ptr<DataClient>;

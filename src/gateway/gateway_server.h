#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include "gate_task.h"
#include "gate_task_manager.h"
#include "gate_user.h"
#include "gate_user_manager.h"
#include "gateway_client.h"
#include "message.h"
#include "network.h"
#include "singleton.h"

class GatewayServer : public Singleton<GatewayServer>
{
    friend class Singleton<GatewayServer>;

public:
    ~GatewayServer();

    // 禁止拷贝和移动
    GatewayServer(const GatewayServer&)            = delete;
    GatewayServer& operator=(const GatewayServer&) = delete;
    GatewayServer(GatewayServer&&)                 = delete;
    GatewayServer& operator=(GatewayServer&&)      = delete;

    // 启动网关服务器
    bool Start();

    // 停止网关服务器
    void Stop();

    // 等待停止
    void WaitForStop();

    // 获取更新间隔（毫秒）
    uint32_t getUpdateIntervalMs() const
    {
        return update_interval_ms_;
    }

    // 处理消息（定时器回调）
    void processMessages();

    // 获取用户管理器
    GateUserManagerPtr getUserManager() const
    {
        return user_manager_;
    }

    // 获取任务管理器
    GateTaskManagerPtr getTaskManager() const
    {
        return task_manager_;
    }

    // 获取网关客户端（用于连接 tinyserver）
    GatewayClientPtr getServerClient() const
    {
        return server_client_;
    }

private:
    GatewayServer();

    // 处理客户端连接
    void onClientConnected(tcp::socket&& socket);

    // 处理来自 tinyserver 的消息
    void onServerMessage(const NetworkMessage& message);

    // 处理与 tinyserver 的连接结果
    void onServerConnected(bool success, const std::string& error_msg);

    // 连接到 tinyserver
    bool connectToTinyServer();

    // 转发消息到 tinyserver
    void forwardToServer(uint32_t user_id, const NetworkMessage& message);

    // 转发消息到客户端
    void forwardToClient(uint32_t user_id, const NetworkMessage& message);

    // 处理任务消息
    void onTaskMessage(GateTaskPtr task, const NetworkMessage& message);

    // 处理任务断开
    void onTaskDisconnected(GateTaskPtr task);

private:
    std::shared_ptr<Acceptor> acceptor_;       // 客户端连接器
    GatewayClientPtr          server_client_;  // 连接到 tinyserver 的客户端

    GateTaskManagerPtr task_manager_;  // 任务管理器
    GateUserManagerPtr user_manager_;  // 用户管理器

    uint32_t    update_interval_ms_{10};  // 更新间隔
    std::string gateway_id_;              // 网关ID
    bool        running_{false};          // 运行状态
};

#define sGatewayServer GatewayServer::getMe()
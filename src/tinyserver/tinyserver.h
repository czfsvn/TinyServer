#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include "network.h"
#include "service.h"
#include "singleton.h"

class TinyServer : public cncpp::Service, public cncpp::Singleton<TinyServer>
{
public:
    TinyServer();
    ~TinyServer();

    // 禁止拷贝和移动
    TinyServer(const TinyServer&)            = delete;
    TinyServer& operator=(const TinyServer&) = delete;
    TinyServer(TinyServer&&)                 = delete;
    TinyServer& operator=(TinyServer&&)      = delete;

    /**
     * @brief 派生类初始化钩子
     * @return 是否成功
     */
    bool onInit() override;

    /**
     * @brief 派生类启动钩子
     * @return 是否成功
     */
    bool onStart() override;

    /**
     * @brief 派生类停止钩子
     */
    void onStop() override;

    /**
     * @brief 派生类更新钩子
     * @return 是否成功
     */
    bool onTick() override;
    /*
    // 是否正在运行
    bool isRunning() const;

    // 获取当前时间戳tick
    uint64_t getCurrentTickMs() const;

    // 设置更新间隔
    void setUpdateIntervalMs(uint32_t interval_ms);

    // 获取更新间隔
    uint32_t getUpdateIntervalMs() const;
*/
private:
    void finalAll();

    bool initGame();
    void gameUpdate();

    // 处理新连接
    void onConnectionCreated(tcp::socket&& sock);

    // 处理消息
    void onMessageReceived(const cncpp::NetworkMessage& message, const std::string& session_info);

    void tick();

    void stopAcceptor();
    bool initAcceptor();
    bool startAcceptor();

    void closeAllSessions();

private:
    std::shared_ptr<cncpp::Acceptor> acceptor_;
    uint64_t                         last_tick_ms_       = 0;
    uint32_t                         update_interval_ms_ = 0;
    bool                             is_running_         = false;
};

#define sTinyServer TinyServer::getMe()
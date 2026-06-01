#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// 用户状态枚举
enum class UserState
{
    Disconnected,   // 未连接
    Connected,      // 已连接
    Authenticated,  // 已认证
    Online,         // 在线
    Offline         // 离线
};

// 用户数据类
class GateUser
{
public:
    GateUser(uint32_t user_id);
    ~GateUser() = default;

    GateUser(const GateUser&)            = delete;
    GateUser& operator=(const GateUser&) = delete;
    GateUser(GateUser&&)                 = delete;
    GateUser& operator=(GateUser&&)      = delete;

    // 获取用户ID
    uint32_t getUserID() const { return user_id_; }

    // 获取/设置状态
    UserState getState() const { return state_.load(); }
    void setState(UserState state) { state_.store(state); }

    // 判断状态
    bool isConnected() const { return state_.load() == UserState::Connected; }
    bool isAuthenticated() const { return state_.load() >= UserState::Authenticated; }
    bool isOnline() const { return state_.load() == UserState::Online; }
    bool isOffline() const { return state_.load() == UserState::Offline; }

    // 获取/设置网关ID
    const std::string& getGatewayID() const;
    void setGatewayID(const std::string& gateway_id);

    // 获取/设置IP地址
    const std::string& getIPAddress() const;
    void setIPAddress(const std::string& ip_address);

    // 获取连接时间
    std::chrono::steady_clock::time_point getConnectTime() const;
    void setConnectTime();

    // 获取/设置认证令牌
    const std::string& getToken() const;
    void setToken(const std::string& token);

    // 获取/设置消息计数
    uint32_t getMessageCount() const { return message_count_.load(); }
    void incrementMessageCount() { message_count_.fetch_add(1); }
    void resetMessageCount() { message_count_.store(0); }

    // 获取最后活跃时间
    std::chrono::steady_clock::time_point getLastActiveTime() const;
    void updateLastActiveTime();

    // 判断是否超时
    bool isTimeout(std::chrono::seconds timeout = std::chrono::seconds(300)) const;

    // 设置自定义属性
    void setProperty(const std::string& key, const std::string& value);
    std::string getProperty(const std::string& key) const;
    bool hasProperty(const std::string& key) const;
    void removeProperty(const std::string& key);

private:
    uint32_t                                     user_id_;
    std::atomic<UserState>                       state_;
    
    mutable std::mutex                           data_mutex_;
    std::string                                  gateway_id_;
    std::string                                  ip_address_;
    std::chrono::steady_clock::time_point        connect_time_;
    std::chrono::steady_clock::time_point        last_active_time_;
    std::string                                  token_;
    
    std::atomic<uint32_t>                        message_count_{0};
    
    mutable std::mutex                           props_mutex_;
    std::unordered_map<std::string, std::string> properties_;
};

using GateUserPtr = std::shared_ptr<GateUser>;

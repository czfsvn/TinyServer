#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
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

    // 禁止拷贝和移动
    GateUser(const GateUser&)            = delete;
    GateUser& operator=(const GateUser&) = delete;
    GateUser(GateUser&&)                 = delete;
    GateUser& operator=(GateUser&&)      = delete;

    // 获取用户ID
    uint32_t getUserID() const
    {
        return user_id_;
    }

    // 获取/设置状态
    UserState getState() const
    {
        return state_;
    }
    void setState(UserState state)
    {
        state_ = state;
    }

    // 判断状态
    bool isConnected() const
    {
        return state_ == UserState::Connected;
    }
    bool isAuthenticated() const
    {
        return state_ >= UserState::Authenticated;
    }
    bool isOnline() const
    {
        return state_ == UserState::Online;
    }
    bool isOffline() const
    {
        return state_ == UserState::Offline;
    }

    // 获取/设置网关ID
    const std::string& getGatewayID() const
    {
        return gateway_id_;
    }
    void setGatewayID(const std::string& gateway_id)
    {
        gateway_id_ = gateway_id;
    }

    // 获取/设置IP地址
    const std::string& getIPAddress() const
    {
        return ip_address_;
    }
    void setIPAddress(const std::string& ip_address)
    {
        ip_address_ = ip_address;
    }

    // 获取连接时间
    std::chrono::steady_clock::time_point getConnectTime() const
    {
        return connect_time_;
    }
    void setConnectTime()
    {
        connect_time_ = std::chrono::steady_clock::now();
    }

    // 获取/设置认证令牌
    const std::string& getToken() const
    {
        return token_;
    }
    void setToken(const std::string& token)
    {
        token_ = token;
    }

    // 获取/设置消息计数
    uint32_t getMessageCount() const
    {
        return message_count_;
    }
    void incrementMessageCount()
    {
        message_count_++;
    }
    void resetMessageCount()
    {
        message_count_ = 0;
    }

    // 获取最后活跃时间
    std::chrono::steady_clock::time_point getLastActiveTime() const
    {
        return last_active_time_;
    }
    void updateLastActiveTime()
    {
        last_active_time_ = std::chrono::steady_clock::now();
    }

    // 判断是否超时（默认 300 秒超时）
    bool isTimeout(std::chrono::seconds timeout = std::chrono::seconds(300)) const;

    // 设置自定义属性
    void        setProperty(const std::string& key, const std::string& value);
    std::string getProperty(const std::string& key) const;
    bool        hasProperty(const std::string& key) const;
    void        removeProperty(const std::string& key);

private:
    uint32_t                                     user_id_;                         // 用户唯一标识（uint32_t）
    UserState                                    state_{UserState::Disconnected};  // 用户状态
    std::string                                  gateway_id_;                      // 所属网关ID
    std::string                                  ip_address_;                      // 客户端IP地址
    std::chrono::steady_clock::time_point        connect_time_;                    // 连接时间
    std::chrono::steady_clock::time_point        last_active_time_;                // 最后活跃时间
    std::string                                  token_;                           // 认证令牌
    uint32_t                                     message_count_{0};                // 消息计数
    std::unordered_map<std::string, std::string> properties_;                      // 自定义属性
};

using GateUserPtr = std::shared_ptr<GateUser>;
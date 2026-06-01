#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "gate_user.h"
#include "singleton.h"

class GateUserManager : public cncpp::Singleton<GateUserManager>
{
public:
    GateUserManager();
    ~GateUserManager();

    GateUserManager(const GateUserManager&)            = delete;
    GateUserManager& operator=(const GateUserManager&) = delete;
    GateUserManager(GateUserManager&&)                 = delete;
    GateUserManager& operator=(GateUserManager&&)      = delete;

    // 添加用户
    GateUserPtr addUser(uint32_t user_id);

    // 获取用户
    GateUserPtr getUser(uint32_t user_id);

    // 移除用户
    void removeUser(uint32_t user_id);

    // 判断用户是否存在
    bool hasUser(uint32_t user_id) const;

    // 获取用户数量
    size_t getUserCount() const;

    // 获取所有用户ID
    std::vector<uint32_t> getAllUserIDs() const;

    // 获取指定状态的用户列表
    std::vector<GateUserPtr> getUsersByState(UserState state) const;

    // 获取在线用户数量
    size_t getOnlineUserCount() const;

    // 获取认证用户数量
    size_t getAuthenticatedUserCount() const;

    // 清理超时用户
    size_t cleanupTimeoutUsers(std::chrono::seconds timeout = std::chrono::seconds(300));

    // 批量更新用户网关ID
    void updateUserGatewayID(uint32_t user_id, const std::string& gateway_id);

    // 设置缓存过期时间
    void     setCacheExpirySeconds(uint32_t seconds);
    uint32_t getCacheExpirySeconds() const;

private:
    mutable std::mutex                        mutex_;
    std::unordered_map<uint32_t, GateUserPtr> users_;
    uint32_t                                  cache_expiry_seconds_{300};
};

#define sGateUserManager GateUserManager::getMe()

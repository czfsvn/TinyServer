#ifndef MESSAGE_IDS_H
#define MESSAGE_IDS_H

#include <cstdint>

/**
     * @brief 消息ID枚举定义
     * 集中管理所有消息类型的唯一标识
     */
enum class MessageId : uint32_t
{
    // ========== 系统预留消息 (1-999) ==========
    HEARTBEAT = 1,  // 心跳消息
    HANDSHAKE = 2,  // 握手消息
    ERROR     = 3,  // 错误消息
    RESPONSE  = 4,  // 响应消息
    AUTH      = 5,  // 认证消息

    // ========== 用户业务消息 (1000-1999) ==========
    LOGIN_REQUEST      = 1001,  // 登录请求
    LOGIN_RESPONSE     = 1002,  // 登录响应
    REGISTER_REQUEST   = 1003,  // 注册请求
    REGISTER_RESPONSE  = 1004,  // 注册响应
    LOGOUT_REQUEST     = 1005,  // 登出请求
    USER_INFO_REQUEST  = 1006,  // 用户信息请求
    USER_INFO_RESPONSE = 1007,  // 用户信息响应

    // ========== 游戏逻辑消息 (2000-2999) ==========
    ENTER_GAME_REQUEST  = 2001,  // 进入游戏请求
    ENTER_GAME_RESPONSE = 2002,  // 进入游戏响应
    MOVE_REQUEST        = 2003,  // 角色移动请求
    MOVE_NOTIFY         = 2004,  // 角色移动通知
    ATTACK_REQUEST      = 2005,  // 攻击请求
    ATTACK_NOTIFY       = 2006,  // 攻击结果通知
    SKILL_REQUEST       = 2007,  // 技能释放请求
    SKILL_NOTIFY        = 2008,  // 技能释放通知

    // ========== 社交系统消息 (3000-3999) ==========
    FRIEND_LIST_REQUEST   = 3001,  // 好友列表请求
    FRIEND_LIST_RESPONSE  = 3002,  // 好友列表响应
    ADD_FRIEND_REQUEST    = 3003,  // 添加好友请求
    ADD_FRIEND_RESPONSE   = 3004,  // 添加好友响应
    DELETE_FRIEND_REQUEST = 3005,  // 删除好友请求
    PRIVATE_CHAT          = 3006,  // 私聊消息
    GUILD_LIST_REQUEST    = 3007,  // 公会列表请求
    GUILD_LIST_RESPONSE   = 3008,  // 公会列表响应

    // ========== 数据库操作消息 (4000-4999) ==========
    MSG_ID_USER_LOGIN    = 4001,  // 用户登录
    MSG_ID_USER_REGISTER = 4002,  // 用户注册
    MSG_ID_USER_QUERY    = 4003,  // 用户查询
    MSG_ID_USER_UPDATE   = 4004,  // 用户更新
    MSG_ID_USER_DELETE   = 4005,  // 用户删除
    MSG_ID_EXECUTE_SQL   = 4006,  // 执行SQL
};

/**
     * @brief 将消息ID枚举转换为uint32_t
     */
inline uint32_t toUint32(MessageId message_id)
{
    return static_cast<uint32_t>(message_id);
}

#endif  // MESSAGE_IDS_H

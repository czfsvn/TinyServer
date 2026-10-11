#ifndef RAW_STRUCTS_H
#define RAW_STRUCTS_H

#include <cstdint>

// 裸结构体（术语见 CONTEXT.md）：1 字节对齐 + 主机序小端，服务端靠强转访问字段。
//
// pack(push, 1) 是硬要求，不是优化：alignof(T) == 1 是 RAW_STRUCT 宏与
// NetworkMessage::as<T>() 用来证明"确实对齐过"的编译期证据，忘写就是编译失败。
//
// push/pop 紧贴每个结构体，不跨命名空间边界 —— 免得后面加别的结构体时
// 有人忘了 pop，把对齐设置漏给后面的代码。
namespace cncpp
{
#pragma pack(push, 1)
    // 认证载荷：tinyclient -> tinyserver
    struct AuthPayload
    {
        uint32_t user_id;
    };
#pragma pack(pop)

#pragma pack(push, 1)
    // 心跳载荷
    struct HeartbeatPayload
    {
        uint32_t gateway_id;
        uint32_t timestamp;
    };
#pragma pack(pop)
}  // namespace cncpp

#endif  // RAW_STRUCTS_H

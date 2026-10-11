#ifndef MESSAGE_NAMES_H
#define MESSAGE_NAMES_H

#include <cstdint>

namespace cncpp
{
    // 消息名字：业务分发（交给哪个 handler 处理）的键。
    //
    // v2 帧头删掉了 message_id_，整数那一套 MsgID 枚举随之废弃（ADR-0011），
    // 分发键统一改成字符串。注意两种格式下这个名字的来历不同：
    //   - protobuf 帧：名字就是 msgname，**必须是真实存在的全限定 protobuf 类型名**，
    //     否则解码阶段在 DescriptorPool 里就查不到（见 CONTEXT.md）；
    //   - 裸结构体帧：线上字节是 msgindex，名字由 RAW_STRUCT 宏在注册时绑定，
    //     因此可以是任意字符串，但不要长得像 protobuf 全限定名，免得看混。
    namespace message_name
    {
        // ---- 裸结构体帧 ----
        constexpr char kAuth[]      = "Auth";
        constexpr char kHeartbeat[] = "Heartbeat";

        // ---- protobuf 帧（对应 src/protos/tiny.proto）----
        constexpr char kVerify[] = "cncpp.VerifyMessage";

        // ---- dataserver ----
        // 这几个目前只作为分发键使用（data_processor 的注册/查找），线上还没有
        // 对应的 protobuf 类型。等 .proto 里有真类型了，名字必须与全限定名一致，
        // 否则解码阶段在 DescriptorPool 里查不到。
        constexpr char kUserLogin[]    = "cncpp.UserLogin";
        constexpr char kUserRegister[] = "cncpp.UserRegister";
        constexpr char kUserQuery[]    = "cncpp.UserQuery";
        constexpr char kUserUpdate[]   = "cncpp.UserUpdate";
        constexpr char kUserDelete[]   = "cncpp.UserDelete";
        constexpr char kExecuteSql[]   = "cncpp.ExecuteSql";
    }  // namespace message_name

    // 裸结构体帧的 msgindex（线上字节，body 前 4 字节大端）。
    // 与上面的名字一一对应，绑定关系在 RAW_STRUCT 宏里建立。
    namespace msg_index
    {
        constexpr uint32_t kAuth      = 1;
        constexpr uint32_t kHeartbeat = 2;
    }                                 // namespace msg_index
}  // namespace cncpp

#endif  // MESSAGE_NAMES_H

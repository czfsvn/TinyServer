#ifndef PROTOBUF_UTIL_H
#define PROTOBUF_UTIL_H

#include <google/protobuf/message.h>
#include <string>

namespace cncpp
{
    /**
     * @brief Protobuf 工具类
     * 用于序列化和反序列化 protobuf 消息
     */
    class ProtobufUtil
    {
    public:
        /**
         * @brief 序列化 protobuf 消息为字符串
         * @param message protobuf 消息
         * @return 序列化后的字符串
         */
        static std::string Serialize(const google::protobuf::Message& message);

        /**
         * @brief 反序列化字符串为 protobuf 消息
         * @param data 序列化的数据
         * @param message 目标 protobuf 消息
         * @return 是否反序列化成功
         */
        static bool Deserialize(const std::string& data, google::protobuf::Message& message);
    };

}  // namespace cncpp

#endif  // PROTOBUF_UTIL_H
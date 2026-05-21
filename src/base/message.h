#ifndef MESSAGE_H
#define MESSAGE_H

#include <boost/asio.hpp>
#include <string>

using boost::asio::ip::tcp;

namespace cncpp
{
    struct MessageHeader
    {
        uint32_t magic_;        // 魔数，用于标识消息
        uint32_t version_;      // 版本号
        uint32_t body_length_;  // 消息体长度
        uint32_t message_id_;   // 消息ID
        uint32_t flags_;        // 标志位（加密、压缩等）

        MessageHeader() : magic_(0x12345678), version_(1), body_length_(0), message_id_(0), flags_(0)
        {
        }
    };

    struct NetworkMessage
    {
        MessageHeader header_;
        std::string body_;
        tcp::endpoint sender_;

        // 默认构造函数
        NetworkMessage()
        {
        }

        NetworkMessage(const std::string& body, const tcp::endpoint& sender) : body_(body), sender_(sender)
        {
            header_.body_length_ = static_cast<uint32_t>(body.size());
        }

        NetworkMessage(const MessageHeader& header, const std::string& body, const tcp::endpoint& sender)
            : header_(header), body_(body), sender_(sender)
        {
        }
    };

}  // namespace cncpp

#endif  // MESSAGE_H
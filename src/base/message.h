#ifndef MESSAGE_H
#define MESSAGE_H

#include <boost/asio.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "message_data.h"

using boost::asio::ip::tcp;

namespace cncpp
{
    // ======== 帧头（术语见 CONTEXT.md 的「帧」）========
    //
    // 线上布局，定长 16 字节，全部网络字节序（大端）：
    //   magic(4) | version(1) | format(1) | flags(2) | body_length(4) | message_id(4)
    //
    // 内存里的 MessageHeader **不是**这个布局：它是主机字节序的普通结构体，
    // 只能通过 encodeHeader / decodeHeader 与字节流互转。
    // 不要对它 memcpy —— 那会把字节序和对齐假设重新引进来。

    constexpr uint32_t kMessageMagic   = 0x12345678;
    constexpr uint8_t  kMessageVersion = 1;
    constexpr size_t   kHeaderSize     = 16;

    // 单帧 body 的上限。必须在按 body_length_ 分配内存之前校验，
    // 否则一个伪造的头部就能让服务端分配 4GB。
    constexpr uint32_t kMaxBodyLength = 1024 * 1024;

    // 载荷格式：body 里装的是什么。互斥取值，不是位标志（ADR-0006）。
    enum class PayloadFormat : uint8_t
    {
        kRaw      = 0,  // 不透明字节，不做解码
        kProtobuf = 1,  // 某个已注册 protobuf 类型的序列化结果
    };

    // 传输层变换标志，与 format 正交：加密与压缩作用在字节层面，
    // 不改变 body 是什么。
    enum : uint16_t
    {
        kFlagCompressed = 0x01,
        kFlagEncrypted  = 0x02,
    };

    struct MessageHeader
    {
        uint32_t      magic_       = kMessageMagic;
        uint8_t       version_     = kMessageVersion;
        PayloadFormat format_      = PayloadFormat::kRaw;
        uint16_t      flags_       = 0;
        uint32_t      body_length_ = 0;
        uint32_t      message_id_  = 0;
    };

    // 帧头解码与校验的结果。
    enum class HeaderError
    {
        kOk,
        kBadMagic,  // 唯一真正失去帧同步、无法恢复的情况
        kBadVersion,
        kBadFormat,
        kBodyTooLarge,
    };

    inline const char* toString(HeaderError err)
    {
        switch (err)
        {
            case HeaderError::kOk: return "ok";
            case HeaderError::kBadMagic: return "bad magic, frame sync lost";
            case HeaderError::kBadVersion: return "unsupported version";
            case HeaderError::kBadFormat: return "unknown payload format";
            case HeaderError::kBodyTooLarge: return "body length exceeds limit";
        }
        return "unknown";
    }

    namespace detail
    {
        inline uint32_t loadBe32(const unsigned char* p)
        {
            return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
                 | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
        }

        inline void storeBe32(unsigned char* p, uint32_t v)
        {
            p[0] = static_cast<unsigned char>(v >> 24);
            p[1] = static_cast<unsigned char>(v >> 16);
            p[2] = static_cast<unsigned char>(v >> 8);
            p[3] = static_cast<unsigned char>(v);
        }

        inline uint16_t loadBe16(const unsigned char* p)
        {
            return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
        }

        inline void storeBe16(unsigned char* p, uint16_t v)
        {
            p[0] = static_cast<unsigned char>(v >> 8);
            p[1] = static_cast<unsigned char>(v);
        }
    }  // namespace detail

    // 把帧头写进至少 kHeaderSize 字节的缓冲区。
    inline void encodeHeader(const MessageHeader& header, void* out)
    {
        auto* p = static_cast<unsigned char*>(out);
        detail::storeBe32(p, header.magic_);
        p[4] = header.version_;
        p[5] = static_cast<unsigned char>(header.format_);
        detail::storeBe16(p + 6, header.flags_);
        detail::storeBe32(p + 8, header.body_length_);
        detail::storeBe32(p + 12, header.message_id_);
    }

    // 从 kHeaderSize 字节的缓冲区读出帧头并校验。
    // 返回非 kOk 表示这一帧不能用，调用方必须关闭连接：body_length 本身就在帧头里，
    // 头部不合格就无从知道该跳过多少字节，帧同步已经丢了（ADR-0009）。
    inline HeaderError decodeHeader(const void* in, MessageHeader& out)
    {
        const auto* p = static_cast<const unsigned char*>(in);

        const uint32_t magic = detail::loadBe32(p);
        if (magic != kMessageMagic)
        {
            return HeaderError::kBadMagic;
        }

        const uint8_t version = p[4];
        if (version != kMessageVersion)
        {
            return HeaderError::kBadVersion;
        }

        const uint8_t format = p[5];
        if (format != static_cast<uint8_t>(PayloadFormat::kRaw)
            && format != static_cast<uint8_t>(PayloadFormat::kProtobuf))
        {
            return HeaderError::kBadFormat;
        }

        const uint32_t body_length = detail::loadBe32(p + 8);
        if (body_length > kMaxBodyLength)
        {
            return HeaderError::kBodyTooLarge;
        }

        out.magic_       = magic;
        out.version_     = version;
        out.format_      = static_cast<PayloadFormat>(format);
        out.flags_       = detail::loadBe16(p + 6);
        out.body_length_ = body_length;
        out.message_id_  = detail::loadBe32(p + 12);
        return HeaderError::kOk;
    }

    struct NetworkMessage
    {
        MessageHeader                 header_;
        tcp::endpoint                 sender_;
        std::unique_ptr<IMessageData> data_;

        NetworkMessage()
        {
        }

        explicit NetworkMessage(const tcp::endpoint& sender) : sender_(sender)
        {
        }

        NetworkMessage(const MessageHeader& header, const tcp::endpoint& sender) : header_(header), sender_(sender)
        {
        }

        NetworkMessage(const MessageHeader& header, std::unique_ptr<IMessageData> data, const tcp::endpoint& sender)
            : header_(header), sender_(sender), data_(std::move(data))
        {
        }

        NetworkMessage(NetworkMessage&& other) noexcept
            : header_(other.header_), sender_(other.sender_), data_(std::move(other.data_))
        {
        }

        NetworkMessage& operator=(NetworkMessage&& other) noexcept
        {
            if (this != &other)
            {
                header_ = other.header_;
                sender_ = other.sender_;
                data_   = std::move(other.data_);
            }
            return *this;
        }

        NetworkMessage(const NetworkMessage& other)
            : header_(other.header_), sender_(other.sender_), data_(other.data_ ? other.data_->clone() : nullptr)
        {
        }

        NetworkMessage& operator=(const NetworkMessage& other)
        {
            if (this != &other)
            {
                header_ = other.header_;
                sender_ = other.sender_;
                data_.reset(other.data_ ? other.data_->clone() : nullptr);
            }
            return *this;
        }

        const std::string* getBody() const
        {
            return data_ ? data_->getBody() : nullptr;
        }

        const google::protobuf::Message* getProtobuf() const
        {
            return data_ ? data_->getProtobuf() : nullptr;
        }

        google::protobuf::Message* getMutableProtobuf()
        {
            return data_ ? data_->getMutableProtobuf() : nullptr;
        }

        bool isProtobuf() const
        {
            return data_ && data_->isProtobuf();
        }

        size_t memorySize() const
        {
            size_t size = sizeof(*this);
            if (data_)
            {
                size += data_->memorySize();
            }
            return size;
        }

        std::string serialize() const
        {
            return data_ ? data_->serialize() : std::string();
        }

        void setBody(std::string body)
        {
            data_ = cncpp::createTextMessage(std::move(body));
        }

        void setProtobuf(std::shared_ptr<google::protobuf::Message> proto)
        {
            data_ = cncpp::createProtobufMessage(std::move(proto));
        }

        static NetworkMessage createTextMessage(const MessageHeader& header, std::string body,
                                                const tcp::endpoint& sender)
        {
            NetworkMessage msg(header, sender);
            msg.setBody(std::move(body));
            return msg;
        }

        static NetworkMessage createProtobufMessage(const MessageHeader&                       header,
                                                    std::shared_ptr<google::protobuf::Message> proto,
                                                    const tcp::endpoint&                       sender)
        {
            NetworkMessage msg(header, sender);
            msg.setProtobuf(std::move(proto));
            return msg;
        }

        // ======== 反射相关方法 ========

        /**
         * @brief 获取消息类型名称
         * @return 消息类型名称，如果不是protobuf消息返回空字符串
         */
        std::string getTypeName() const
        {
            const auto* proto = getProtobuf();
            if (!proto)
            {
                return {};
            }

            return proto->GetDescriptor()->full_name();
        }

        /**
         * @brief 获取字段值（字符串形式）
         * @param field_name 字段名称
         * @return 字段值的字符串表示，如果字段不存在返回空字符串
         */
        std::string getFieldValue(const std::string& field_name) const
        {
            const auto* proto = getProtobuf();
            if (!proto)
            {
                return {};
            }

            const google::protobuf::Descriptor*      descriptor       = proto->GetDescriptor();
            const google::protobuf::FieldDescriptor* field_descriptor = descriptor->FindFieldByName(field_name);

            if (!field_descriptor)
            {
                return {};
            }

            const google::protobuf::Reflection* reflection = proto->GetReflection();
            if (!reflection)
            {
                return {};
            }

            return getFieldValueAsString(*proto, field_descriptor, reflection);
        }

        /**
         * @brief 设置字段值
         * @param field_name 字段名称
         * @param value 字段值（字符串形式）
         * @return 是否设置成功
         */
        bool setFieldValue(const std::string& field_name, const std::string& value)
        {
            auto* proto = getMutableProtobuf();
            if (!proto)
            {
                return false;
            }

            const google::protobuf::Descriptor*      descriptor       = proto->GetDescriptor();
            const google::protobuf::FieldDescriptor* field_descriptor = descriptor->FindFieldByName(field_name);

            if (!field_descriptor)
            {
                return false;
            }

            const google::protobuf::Reflection* reflection = proto->GetReflection();
            if (!reflection)
            {
                return false;
            }

            return setFieldValueFromString(*proto, field_descriptor, reflection, value);
        }

        /**
         * @brief 检查是否存在指定字段
         * @param field_name 字段名称
         * @return 是否存在该字段
         */
        bool hasField(const std::string& field_name) const
        {
            const auto* proto = getProtobuf();
            if (!proto)
            {
                return false;
            }

            const google::protobuf::Descriptor* descriptor = proto->GetDescriptor();
            return descriptor->FindFieldByName(field_name) != nullptr;
        }

        /**
         * @brief 检查字段是否被设置
         * @param field_name 字段名称
         * @return 字段是否已设置值
         */
        bool isFieldSet(const std::string& field_name) const
        {
            const auto* proto = getProtobuf();
            if (!proto)
            {
                return false;
            }

            const google::protobuf::Descriptor*      descriptor       = proto->GetDescriptor();
            const google::protobuf::FieldDescriptor* field_descriptor = descriptor->FindFieldByName(field_name);

            if (!field_descriptor)
            {
                return false;
            }

            const google::protobuf::Reflection* reflection = proto->GetReflection();
            if (!reflection)
            {
                return false;
            }

            return reflection->HasField(*proto, field_descriptor);
        }

    private:
        /**
         * @brief 将字段值转换为字符串
         */
        std::string getFieldValueAsString(const google::protobuf::Message&         message,
                                          const google::protobuf::FieldDescriptor* field_descriptor,
                                          const google::protobuf::Reflection*      reflection) const
        {
            if (field_descriptor->is_repeated())
            {
                int         count  = reflection->FieldSize(message, field_descriptor);
                std::string result = "[";
                for (int i = 0; i < count; ++i)
                {
                    if (i > 0)
                    {
                        result += ", ";
                    }
                    result += getSingleFieldValue(message, field_descriptor, reflection, i);
                }
                result += "]";
                return result;
            }
            else
            {
                return getSingleFieldValue(message, field_descriptor, reflection, -1);
            }
        }

        /**
         * @brief 获取单个字段值的字符串表示
         */
        std::string getSingleFieldValue(const google::protobuf::Message&         message,
                                        const google::protobuf::FieldDescriptor* field_descriptor,
                                        const google::protobuf::Reflection* reflection, int index) const
        {
            using namespace google::protobuf;

            switch (field_descriptor->cpp_type())
            {
                case FieldDescriptor::CPPTYPE_INT32:
                    return std::to_string(index >= 0 ? reflection->GetRepeatedInt32(message, field_descriptor, index)
                                                     : reflection->GetInt32(message, field_descriptor));
                case FieldDescriptor::CPPTYPE_INT64:
                    return std::to_string(index >= 0 ? reflection->GetRepeatedInt64(message, field_descriptor, index)
                                                     : reflection->GetInt64(message, field_descriptor));
                case FieldDescriptor::CPPTYPE_UINT32:
                    return std::to_string(index >= 0 ? reflection->GetRepeatedUInt32(message, field_descriptor, index)
                                                     : reflection->GetUInt32(message, field_descriptor));
                case FieldDescriptor::CPPTYPE_UINT64:
                    return std::to_string(index >= 0 ? reflection->GetRepeatedUInt64(message, field_descriptor, index)
                                                     : reflection->GetUInt64(message, field_descriptor));
                case FieldDescriptor::CPPTYPE_FLOAT:
                    return std::to_string(index >= 0 ? reflection->GetRepeatedFloat(message, field_descriptor, index)
                                                     : reflection->GetFloat(message, field_descriptor));
                case FieldDescriptor::CPPTYPE_DOUBLE:
                    return std::to_string(index >= 0 ? reflection->GetRepeatedDouble(message, field_descriptor, index)
                                                     : reflection->GetDouble(message, field_descriptor));
                case FieldDescriptor::CPPTYPE_BOOL:
                    return (index >= 0 ? reflection->GetRepeatedBool(message, field_descriptor, index)
                                       : reflection->GetBool(message, field_descriptor))
                               ? "true"
                               : "false";
                case FieldDescriptor::CPPTYPE_STRING:
                    return index >= 0 ? reflection->GetRepeatedString(message, field_descriptor, index)
                                      : reflection->GetString(message, field_descriptor);
                case FieldDescriptor::CPPTYPE_MESSAGE: {
                    const Message& sub_msg = index >= 0
                                                 ? reflection->GetRepeatedMessage(message, field_descriptor, index)
                                                 : reflection->GetMessage(message, field_descriptor);
                    return sub_msg.ShortDebugString();
                }
                case FieldDescriptor::CPPTYPE_ENUM: {
                    const EnumValueDescriptor* enum_value
                        = index >= 0 ? reflection->GetRepeatedEnum(message, field_descriptor, index)
                                     : reflection->GetEnum(message, field_descriptor);
                    return enum_value ? enum_value->name() : std::string();
                }
                default:
                    return "";
            }
        }

        /**
         * @brief 从字符串设置字段值
         */
        bool setFieldValueFromString(google::protobuf::Message&               message,
                                     const google::protobuf::FieldDescriptor* field_descriptor,
                                     const google::protobuf::Reflection* reflection, const std::string& value)
        {
            using namespace google::protobuf;

            switch (field_descriptor->cpp_type())
            {
                case FieldDescriptor::CPPTYPE_INT32:
                    reflection->SetInt32(&message, field_descriptor, std::stoi(value));
                    return true;
                case FieldDescriptor::CPPTYPE_INT64:
                    reflection->SetInt64(&message, field_descriptor, std::stoll(value));
                    return true;
                case FieldDescriptor::CPPTYPE_UINT32:
                    reflection->SetUInt32(&message, field_descriptor, std::stoul(value));
                    return true;
                case FieldDescriptor::CPPTYPE_UINT64:
                    reflection->SetUInt64(&message, field_descriptor, std::stoull(value));
                    return true;
                case FieldDescriptor::CPPTYPE_FLOAT:
                    reflection->SetFloat(&message, field_descriptor, std::stof(value));
                    return true;
                case FieldDescriptor::CPPTYPE_DOUBLE:
                    reflection->SetDouble(&message, field_descriptor, std::stod(value));
                    return true;
                case FieldDescriptor::CPPTYPE_BOOL:
                    reflection->SetBool(&message, field_descriptor, (value == "true" || value == "1"));
                    return true;
                case FieldDescriptor::CPPTYPE_STRING:
                    reflection->SetString(&message, field_descriptor, value);
                    return true;
                default:
                    return false;
            }
        }
    };

}  // namespace cncpp

#endif  // MESSAGE_H

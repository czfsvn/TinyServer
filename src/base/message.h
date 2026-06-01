#ifndef MESSAGE_H
#define MESSAGE_H

#include <boost/asio.hpp>
#include <memory>
#include <string>

#include "message_data.h"

using boost::asio::ip::tcp;

namespace cncpp
{
    struct MessageHeader
    {
        uint32_t magic_;
        uint32_t version_;
        uint32_t body_length_;
        uint32_t message_id_;
        uint32_t flags_;

        MessageHeader() : magic_(0x12345678), version_(1), body_length_(0), message_id_(0), flags_(0)
        {
        }
    };

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

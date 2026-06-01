#ifndef PROTOBUF_UTIL_H
#define PROTOBUF_UTIL_H

#include <memory>
#include <string>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/message.h>

#include "logger.h"
#include "message_ids.h"

namespace cncpp
{
    /**
     * @brief Protobuf工具类
     * 提供反射方式的序列化和反序列化功能
     */
    class ProtobufUtil
    {
    public:
        /**
         * @brief 序列化protobuf消息
         * @param message protobuf消息对象
         * @return 序列化后的字节流
         */
        static std::string Serialize(const google::protobuf::Message& message)
        {
            std::string output;
            if (message.SerializeToString(&output))
            {
                return output;
            }
            return {};
        }

        /**
         * @brief 反序列化protobuf消息（已知类型）
         * @param data 序列化的字节流
         * @param message 目标消息对象
         * @return 是否成功
         */
        static bool Deserialize(const std::string& data, google::protobuf::Message& message)
        {
            return message.ParseFromString(data);
        }

        /**
         * @brief 使用反射反序列化protobuf消息（未知类型）
         * @param data 序列化的字节流
         * @param descriptor 消息描述符
         * @return 反序列化后的消息对象，如果失败返回nullptr
         */
        static std::unique_ptr<google::protobuf::Message> DeserializeWithReflection(
            const std::string& data, const google::protobuf::Descriptor* descriptor)
        {
            if (!descriptor)
            {
                LOG_ERROR("Null message descriptor");
                return nullptr;
            }

            // 获取消息工厂
            const google::protobuf::Message* prototype
                = google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor);

            if (!prototype)
            {
                LOG_ERROR("Failed to get message prototype for: {}", descriptor->full_name());
                return nullptr;
            }

            // 创建消息实例
            std::unique_ptr<google::protobuf::Message> message(prototype->New());

            // 反序列化
            if (!message->ParseFromString(data))
            {
                LOG_ERROR("Failed to deserialize message: {}", descriptor->full_name());
                return nullptr;
            }

            return message;
        }

        /**
         * @brief 使用反射反序列化protobuf消息（通过消息名称）
         * @param data 序列化的字节流
         * @param message_name 消息完全限定名（如 "cncpp.LoginRequest"）
         * @return 反序列化后的消息对象，如果失败返回nullptr
         */
        static std::unique_ptr<google::protobuf::Message> DeserializeByName(const std::string& data,
                                                                            const std::string& message_name)
        {
            // 获取消息描述符
            const google::protobuf::Descriptor* descriptor
                = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(message_name);

            if (!descriptor)
            {
                LOG_ERROR("Message descriptor not found: {}", message_name);
                return nullptr;
            }

            return DeserializeWithReflection(data, descriptor);
        }

        /**
         * @brief 获取消息描述符
         * @param message_name 消息完全限定名
         * @return 消息描述符，如果未找到返回nullptr
         */
        static const google::protobuf::Descriptor* GetDescriptor(const std::string& message_name)
        {
            return google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(message_name);
        }

        /**
         * @brief 获取消息字段值（通过反射）
         * @param message protobuf消息对象
         * @param field_name 字段名称
         * @return 字段值的字符串表示
         */
        static std::string GetFieldValue(const google::protobuf::Message& message, const std::string& field_name)
        {
            const google::protobuf::Descriptor*      descriptor       = message.GetDescriptor();
            const google::protobuf::FieldDescriptor* field_descriptor = descriptor->FindFieldByName(field_name);

            if (!field_descriptor)
            {
                LOG_ERROR("Field not found: {}", field_name);
                return {};
            }

            const google::protobuf::Reflection* reflection = message.GetReflection();
            if (!reflection)
            {
                LOG_ERROR("Failed to get reflection");
                return {};
            }

            return GetFieldValueAsString(message, field_descriptor, reflection);
        }

        /**
         * @brief 设置消息字段值（通过反射）
         * @param message protobuf消息对象（非const）
         * @param field_name 字段名称
         * @param value 字段值
         * @return 是否成功
         */
        static bool SetFieldValue(google::protobuf::Message& message, const std::string& field_name,
                                  const std::string& value)
        {
            const google::protobuf::Descriptor*      descriptor       = message.GetDescriptor();
            const google::protobuf::FieldDescriptor* field_descriptor = descriptor->FindFieldByName(field_name);

            if (!field_descriptor)
            {
                LOG_ERROR("Field not found: {}", field_name);
                return false;
            }

            const google::protobuf::Reflection* reflection = message.GetReflection();
            if (!reflection)
            {
                LOG_ERROR("Failed to get reflection");
                return false;
            }

            return SetFieldValueFromString(message, field_descriptor, reflection, value);
        }

        /**
         * @brief 打印消息内容（通过反射）
         * @param message protobuf消息对象
         * @return 消息内容的字符串表示
         */
        static std::string DebugString(const google::protobuf::Message& message)
        {
            return message.DebugString();
        }

        /**
         * @brief 获取消息类型名称
         * @param message protobuf消息对象
         * @return 消息类型完全限定名
         */
        static std::string GetTypeName(const google::protobuf::Message& message)
        {
            return message.GetDescriptor()->full_name();
        }

        /**
         * @brief 检查消息是否为空
         * @param message protobuf消息对象
         * @return 是否为空
         */
        static bool IsEmpty(const google::protobuf::Message& message)
        {
            return message.ByteSizeLong() == 0;
        }

        /**
         * @brief 获取消息大小
         * @param message protobuf消息对象
         * @return 消息字节大小
         */
        static size_t GetSize(const google::protobuf::Message& message)
        {
            return static_cast<size_t>(message.ByteSizeLong());
        }

    private:
        /**
         * @brief 将字段值转换为字符串
         */
        static std::string GetFieldValueAsString(const google::protobuf::Message&         message,
                                                 const google::protobuf::FieldDescriptor* field_descriptor,
                                                 const google::protobuf::Reflection*      reflection)
        {
            using namespace google::protobuf;

            switch (field_descriptor->type())
            {
                case FieldDescriptor::TYPE_INT32:
                    return std::to_string(reflection->GetInt32(message, field_descriptor));
                case FieldDescriptor::TYPE_INT64:
                    return std::to_string(reflection->GetInt64(message, field_descriptor));
                case FieldDescriptor::TYPE_UINT32:
                    return std::to_string(reflection->GetUInt32(message, field_descriptor));
                case FieldDescriptor::TYPE_UINT64:
                    return std::to_string(reflection->GetUInt64(message, field_descriptor));
                case FieldDescriptor::TYPE_FLOAT:
                    return std::to_string(reflection->GetFloat(message, field_descriptor));
                case FieldDescriptor::TYPE_DOUBLE:
                    return std::to_string(reflection->GetDouble(message, field_descriptor));
                case FieldDescriptor::TYPE_BOOL:
                    return reflection->GetBool(message, field_descriptor) ? "true" : "false";
                case FieldDescriptor::TYPE_STRING:
                    return reflection->GetString(message, field_descriptor);
                case FieldDescriptor::TYPE_BYTES: {
                    std::string bytes = reflection->GetString(message, field_descriptor);
                    return "bytes(" + std::to_string(bytes.size()) + ")";
                }
                case FieldDescriptor::TYPE_ENUM: {
                    const EnumValueDescriptor* enum_value = reflection->GetEnum(message, field_descriptor);
                    return enum_value ? enum_value->name() : "unknown";
                }
                default:
                    return "[complex type]";
            }
        }

        /**
         * @brief 从字符串设置字段值
         */
        static bool SetFieldValueFromString(google::protobuf::Message&               message,
                                            const google::protobuf::FieldDescriptor* field_descriptor,
                                            const google::protobuf::Reflection* reflection, const std::string& value)
        {
            using namespace google::protobuf;

            switch (field_descriptor->type())
            {
                case FieldDescriptor::TYPE_INT32:
                    reflection->SetInt32(&message, field_descriptor, std::stoi(value));
                    return true;
                case FieldDescriptor::TYPE_INT64:
                    reflection->SetInt64(&message, field_descriptor, std::stoll(value));
                    return true;
                case FieldDescriptor::TYPE_UINT32:
                    reflection->SetUInt32(&message, field_descriptor, std::stoul(value));
                    return true;
                case FieldDescriptor::TYPE_UINT64:
                    reflection->SetUInt64(&message, field_descriptor, std::stoull(value));
                    return true;
                case FieldDescriptor::TYPE_FLOAT:
                    reflection->SetFloat(&message, field_descriptor, std::stof(value));
                    return true;
                case FieldDescriptor::TYPE_DOUBLE:
                    reflection->SetDouble(&message, field_descriptor, std::stod(value));
                    return true;
                case FieldDescriptor::TYPE_BOOL:
                    reflection->SetBool(&message, field_descriptor, (value == "true" || value == "1"));
                    return true;
                case FieldDescriptor::TYPE_STRING:
                    reflection->SetString(&message, field_descriptor, value);
                    return true;
                case FieldDescriptor::TYPE_BYTES:
                    reflection->SetString(&message, field_descriptor, value);
                    return true;
                case FieldDescriptor::TYPE_ENUM: {
                    const EnumDescriptor*      enum_descriptor = field_descriptor->enum_type();
                    const EnumValueDescriptor* enum_value      = enum_descriptor->FindValueByName(value);
                    if (enum_value)
                    {
                        reflection->SetEnum(&message, field_descriptor, enum_value);
                        return true;
                    }
                    return false;
                }
                default:
                    LOG_ERROR("Unsupported field type: {}", (static_cast<int32_t>(field_descriptor->type())));
                    return false;
            }
        }
    };

}  // namespace cncpp

#endif  // PROTOBUF_UTIL_H

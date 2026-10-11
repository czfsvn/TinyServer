#ifndef MESSAGE_DATA_H
#define MESSAGE_DATA_H

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <google/protobuf/message.h>

#include "protobuf_util.h"

namespace cncpp
{
    /**
     * @brief 消息数据抽象接口（类型擦除）
     *
     * 只承载**载荷**。载荷格式归帧头的 `format_` 管（ADR-0011），这里刻意不自报
     * 格式——否则"帧头解出来的 format"和"data 自报的 format"就是两处真相，加第四
     * 种格式时最容易漏的正是这种同步。
     *
     * `typeName()` 是**解码的产物**、业务分发的键，不是格式：protobuf 帧它就是
     * msgname，裸结构体帧它由注册表从 msgindex 反查而来。
     *
     * 当前只有两个实现（裸结构体 / protobuf）。flatbuffers 只占 `PayloadFormat`
     * 的枚举位、没有实现类——为尚未接入的格式建抽象是投机性的（ADR-0006 的同一
     * 个理由，ADR-0011 重申）。
     */
    class IMessageData
    {
    public:
        virtual ~IMessageData() = default;

        virtual IMessageData* clone() const = 0;

        // 完整 body 字节（含类型标识前缀）。裸结构体帧要读结构体请用
        // NetworkMessage::as<T>()，别自己去算偏移。
        virtual const std::string* getBody() const = 0;

        virtual const google::protobuf::Message* getProtobuf() const = 0;

        virtual google::protobuf::Message* getMutableProtobuf() = 0;

        // 解码得出的消息名字：业务分发的键。
        virtual const std::string& typeName() const = 0;

        // 裸结构体帧的 msgindex。非裸结构体帧返回 0。
        virtual uint32_t typeIndex() const
        {
            return 0;
        }

        virtual size_t memorySize() const = 0;

        virtual std::string serialize() const = 0;
    };

    /**
     * @brief 裸结构体消息数据实现
     *
     * body_ 是完整形态：msgindex(4 字节大端) + 1 字节对齐的结构体字节。
     * 结构体字段是主机序（小端），不做转换——这是"强转"能成立的前提。
     *
     * 字节独占持有，clone() 是深拷贝：as<T>() 返回的视图严格绑定在本对象存活
     * 期内，语义简单、不会有人在对象死后拿着指针还能读到东西。
     */
    class RawStructMessageData : public IMessageData
    {
    public:
        RawStructMessageData(uint32_t msg_index, std::string type_name, std::string body)
            : msg_index_(msg_index), type_name_(std::move(type_name)), body_(std::move(body))
        {
        }

        RawStructMessageData(const RawStructMessageData&)            = default;
        RawStructMessageData(RawStructMessageData&&) noexcept        = default;
        RawStructMessageData& operator=(const RawStructMessageData&) = default;
        RawStructMessageData& operator=(RawStructMessageData&&) noexcept = default;

        IMessageData* clone() const override
        {
            return new RawStructMessageData(*this);
        }

        const std::string* getBody() const override
        {
            return &body_;
        }

        const google::protobuf::Message* getProtobuf() const override
        {
            return nullptr;
        }

        google::protobuf::Message* getMutableProtobuf() override
        {
            return nullptr;
        }

        const std::string& typeName() const override
        {
            return type_name_;
        }

        uint32_t typeIndex() const override
        {
            return msg_index_;
        }

        size_t memorySize() const override
        {
            return sizeof(*this) + body_.capacity() + type_name_.capacity();
        }

        std::string serialize() const override
        {
            return body_;
        }

    private:
        uint32_t    msg_index_;
        std::string type_name_;
        std::string body_;
    };

    /**
     * @brief Protobuf消息数据实现
     *
     * 持有的是 DynamicMessage：类型由 msgname 在 DescriptorPool 里反射得出，
     * 不是编译期的生成类型。因此业务侧只能通过反射 API 访问字段
     * （NetworkMessage::getFieldValue 那一套），dynamic_cast 恒为 nullptr。
     *
     * 不保留原始字节（ADR-0007 的结论在 v2 里平移：类型标识查不到即丢弃，
     * 解码成功后字节不再可得）。
     */
    class ProtobufMessageData : public IMessageData
    {
    public:
        ProtobufMessageData(std::string msg_name, std::shared_ptr<google::protobuf::Message> proto)
            : msg_name_(std::move(msg_name)), proto_(std::move(proto))
        {
        }

        ProtobufMessageData(const ProtobufMessageData& other) : msg_name_(other.msg_name_), proto_(nullptr)
        {
            if (other.proto_)
            {
                proto_ = std::shared_ptr<google::protobuf::Message>(other.proto_->New());
                proto_->CopyFrom(*other.proto_);
            }
        }

        ProtobufMessageData& operator=(const ProtobufMessageData& other)
        {
            if (this != &other)
            {
                msg_name_ = other.msg_name_;
                if (other.proto_)
                {
                    proto_ = std::shared_ptr<google::protobuf::Message>(other.proto_->New());
                    proto_->CopyFrom(*other.proto_);
                }
                else
                {
                    proto_.reset();
                }
            }
            return *this;
        }

        IMessageData* clone() const override
        {
            return new ProtobufMessageData(*this);
        }

        const std::string* getBody() const override
        {
            return nullptr;
        }

        const google::protobuf::Message* getProtobuf() const override
        {
            return proto_.get();
        }

        google::protobuf::Message* getMutableProtobuf() override
        {
            return proto_.get();
        }

        const std::string& typeName() const override
        {
            return msg_name_;
        }

        size_t memorySize() const override
        {
            size_t size = sizeof(*this) + msg_name_.capacity();
            if (proto_)
            {
                size += proto_->ByteSizeLong();
            }
            return size;
        }

        std::string serialize() const override
        {
            return proto_ ? ProtobufUtil::Serialize(*proto_) : std::string();
        }

        const std::shared_ptr<google::protobuf::Message>& proto() const
        {
            return proto_;
        }

        std::shared_ptr<google::protobuf::Message>& proto()
        {
            return proto_;
        }

    private:
        std::string                                 msg_name_;
        std::shared_ptr<google::protobuf::Message>  proto_;
    };

    /**
     * @brief 工厂函数创建裸结构体消息
     *
     * body 必须是完整形态（msgindex 前缀 + 结构体字节），前缀由
     * NetworkMessage::setRaw / Session 的发送路径拼装。
     */
    inline std::unique_ptr<IMessageData> createRawStructMessage(uint32_t msg_index, std::string type_name,
                                                                std::string body)
    {
        return std::make_unique<RawStructMessageData>(msg_index, std::move(type_name), std::move(body));
    }

    /**
     * @brief 工厂函数创建Protobuf消息
     */
    inline std::unique_ptr<IMessageData> createProtobufMessage(std::string                                msg_name,
                                                              std::shared_ptr<google::protobuf::Message> proto)
    {
        return std::make_unique<ProtobufMessageData>(std::move(msg_name), std::move(proto));
    }

}  // namespace cncpp

#endif  // MESSAGE_DATA_H

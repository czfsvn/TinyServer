#ifndef MESSAGE_DATA_H
#define MESSAGE_DATA_H

#include <memory>
#include <string>
#include <vector>

#include <google/protobuf/message.h>

#include "protobuf_util.h"

namespace cncpp
{
    /**
     * @brief 消息数据抽象接口（类型擦除）
     */
    class IMessageData
    {
    public:
        virtual ~IMessageData() = default;

        virtual IMessageData* clone() const = 0;

        virtual const std::string* getBody() const = 0;

        virtual const google::protobuf::Message* getProtobuf() const = 0;

        virtual google::protobuf::Message* getMutableProtobuf() = 0;

        virtual bool isProtobuf() const = 0;

        virtual size_t memorySize() const = 0;

        virtual std::string serialize() const = 0;
    };

    /**
     * @brief 文本消息数据实现
     */
    class TextMessageData : public IMessageData
    {
    public:
        explicit TextMessageData(std::string body) : body_(std::move(body))
        {
        }

        TextMessageData(const TextMessageData& other) : body_(other.body_)
        {
        }

        TextMessageData(TextMessageData&& other) noexcept : body_(std::move(other.body_))
        {
        }

        TextMessageData& operator=(const TextMessageData& other)
        {
            if (this != &other)
            {
                body_ = other.body_;
            }
            return *this;
        }

        TextMessageData& operator=(TextMessageData&& other) noexcept
        {
            if (this != &other)
            {
                body_ = std::move(other.body_);
            }
            return *this;
        }

        IMessageData* clone() const override
        {
            return new TextMessageData(*this);
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

        bool isProtobuf() const override
        {
            return false;
        }

        size_t memorySize() const override
        {
            return sizeof(*this) + body_.capacity();
        }

        std::string serialize() const override
        {
            return body_;
        }

        const std::string& body() const
        {
            return body_;
        }

        std::string& body()
        {
            return body_;
        }

    private:
        std::string body_;
    };

    /**
     * @brief Protobuf消息数据实现
     */
    class ProtobufMessageData : public IMessageData
    {
    public:
        explicit ProtobufMessageData(std::shared_ptr<google::protobuf::Message> proto) : proto_(std::move(proto))
        {
        }

        ProtobufMessageData(const ProtobufMessageData& other) : proto_(nullptr)
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

        bool isProtobuf() const override
        {
            return true;
        }

        size_t memorySize() const override
        {
            size_t size = sizeof(*this);
            if (proto_)
            {
                size += proto_->ByteSizeLong();
            }
            return size;
        }

        std::string serialize() const override
        {
            if (proto_)
            {
                return ProtobufUtil::Serialize(*proto_);
            }
            return {};
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
        std::shared_ptr<google::protobuf::Message> proto_;
    };

    /**
     * @brief 工厂函数创建文本消息
     */
    inline std::unique_ptr<IMessageData> createTextMessage(std::string body)
    {
        return std::make_unique<TextMessageData>(std::move(body));
    }

    /**
     * @brief 工厂函数创建Protobuf消息
     */
    inline std::unique_ptr<IMessageData> createProtobufMessage(std::shared_ptr<google::protobuf::Message> proto)
    {
        return std::make_unique<ProtobufMessageData>(std::move(proto));
    }

}  // namespace cncpp

#endif  // MESSAGE_DATA_H

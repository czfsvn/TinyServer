#ifndef MESSAGE_HANDLER_H
#define MESSAGE_HANDLER_H

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <google/protobuf/message.h>

#include "message.h"
#include "message_ids.h"

namespace cncpp
{
    class MessageHandlerRegistry
    {
    public:
        using MessageCreator = std::function<std::shared_ptr<google::protobuf::Message>()>;
        using MessageHandler = std::function<void(const NetworkMessage&)>;

        static MessageHandlerRegistry& instance()
        {
            static MessageHandlerRegistry instance;
            return instance;
        }

        void registerMessageHandler(uint32_t message_id, MessageCreator creator, MessageHandler handler)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            creators_[message_id] = creator;
            handlers_[message_id] = handler;
        }

        void registerMessageHandler(MessageId message_id, MessageCreator creator, MessageHandler handler)
        {
            registerMessageHandler(static_cast<uint32_t>(message_id), creator, handler);
        }

        void unregisterMessageHandler(uint32_t message_id)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            creators_.erase(message_id);
            handlers_.erase(message_id);
        }

        void unregisterMessageHandler(MessageId message_id)
        {
            unregisterMessageHandler(static_cast<uint32_t>(message_id));
        }

        std::shared_ptr<google::protobuf::Message> createMessage(uint32_t message_id) const
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto                         it = creators_.find(message_id);
            if (it != creators_.end())
            {
                return it->second();
            }
            return nullptr;
        }

        std::shared_ptr<google::protobuf::Message> createMessage(MessageId message_id) const
        {
            return createMessage(static_cast<uint32_t>(message_id));
        }

        bool handleMessage(const NetworkMessage& message) const
        {
            uint32_t message_id = message.header_.message_id_;

            std::unique_lock<std::mutex> lock(mutex_);
            auto                         it = handlers_.find(message_id);
            if (it != handlers_.end())
            {
                MessageHandler handler = it->second;
                lock.unlock();

                try
                {
                    handler(message);
                    return true;
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("Message handler exception for message_id={}: {}", message_id, e.what());
                    return false;
                }
            }
            return false;
        }

        bool isRegistered(uint32_t message_id) const
        {
            std::unique_lock<std::mutex> lock(mutex_);
            return creators_.find(message_id) != creators_.end();
        }

        bool isRegistered(MessageId message_id) const
        {
            return isRegistered(static_cast<uint32_t>(message_id));
        }

    private:
        MessageHandlerRegistry()  = default;
        ~MessageHandlerRegistry() = default;

        MessageHandlerRegistry(const MessageHandlerRegistry&)            = delete;
        MessageHandlerRegistry& operator=(const MessageHandlerRegistry&) = delete;

        mutable std::mutex                           mutex_;
        std::unordered_map<uint32_t, MessageCreator> creators_;
        std::unordered_map<uint32_t, MessageHandler> handlers_;
    };

    /**
     * @brief 消息处理回调包装器
     * 将类型安全的回调转换为统一的 MessageHandler
     */
    template <typename MessageType>
    class MessageHandlerWrapper
    {
    public:
        using TypedHandler = std::function<void(const MessageType*)>;

        static MessageHandlerRegistry::MessageHandler wrap(TypedHandler handler)
        {
            return [handler](const NetworkMessage& msg) {
                const auto* proto = msg.getProtobuf();
                if (!proto)
                {
                    LOG_ERROR("Null protobuf message in handler");
                    return;
                }

                const auto* typed_msg = dynamic_cast<const MessageType*>(proto);
                if (!typed_msg)
                {
                    LOG_ERROR("Failed to cast message to target type");
                    return;
                }

                handler(typed_msg);
            };
        }
    };

    /**
     * @brief 消息处理器注册辅助类
     */
    template <typename MessageType>
    class MessageHandlerRegistrar
    {
    public:
        using TypedHandler = typename MessageHandlerWrapper<MessageType>::TypedHandler;

        explicit MessageHandlerRegistrar(uint32_t message_id, TypedHandler handler) : message_id_(message_id)
        {
            auto wrapped_handler = MessageHandlerWrapper<MessageType>::wrap(std::move(handler));

            MessageHandlerRegistry::instance().registerMessageHandler(message_id_, []() {
                return std::make_shared<MessageType>();
            }, wrapped_handler);
        }

        explicit MessageHandlerRegistrar(MessageId message_id, TypedHandler handler)
            : MessageHandlerRegistrar(static_cast<uint32_t>(message_id), std::move(handler))
        {
        }

        ~MessageHandlerRegistrar()
        {
            MessageHandlerRegistry::instance().unregisterMessageHandler(message_id_);
        }

        MessageHandlerRegistrar(const MessageHandlerRegistrar&)            = delete;
        MessageHandlerRegistrar& operator=(const MessageHandlerRegistrar&) = delete;

    private:
        uint32_t message_id_;
    };

/**
     * @brief 消息注册宏（简洁版本）
     * @param MESSAGE_ID 消息ID（支持 MessageId 枚举或 uint32_t）
     * @param MESSAGE_TYPE protobuf消息类型
     * 
     * 使用方式:
     * PROTO_MSG(MessageId::LOGIN_REQUEST, cncpp::LoginRequest) {
     *     LOG_INFO("Login request from: {}", msg->username());
     * }
     * 
     * 或使用数值:
     * PROTO_MSG(1001, cncpp::LoginRequest) {
     *     LOG_INFO("Login request from: {}", msg->username());
     * }
     */
#define PROTO_MSG(MESSAGE_ID, MESSAGE_TYPE)                                                                  \
    void                                                msg_handler_##MESSAGE_TYPE(const MESSAGE_TYPE* msg); \
    static cncpp::MessageHandlerRegistrar<MESSAGE_TYPE> msg_handler_registrar_##MESSAGE_TYPE(                \
        MESSAGE_ID, msg_handler_##MESSAGE_TYPE);                                                             \
    void msg_handler_##MESSAGE_TYPE(const MESSAGE_TYPE* msg)

}  // namespace cncpp

#endif  // MESSAGE_HANDLER_H

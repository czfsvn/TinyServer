#ifndef MESSAGE_HANDLER_H
#define MESSAGE_HANDLER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "message.h"

namespace cncpp
{
    // ======== 裸结构体注册表 ========
    //
    // msgindex -> {消息名字, sizeof(结构体)}。io 线程在解码阶段查它，校验
    // body_length_ == kRawIndexSize + size，把畸形帧挡在业务层之前 —— 这是
    // ADR-0005 那句"io 线程上的解码必须是有界的"在裸结构体帧上的落地。
    //
    // 注意这里有两个标识，别混（CONTEXT.md）：
    //   msgindex —— 线上的类型标识，解码的输入；
    //   消息名字 —— 业务分发的键。
    // 注册宏要求一次给齐两样，就是为了不让"改了结构体忘了改表"有可乘之机。
    struct RawStructMeta
    {
        std::string name;  // 消息名字：分发的键
        size_t      size;  // sizeof(裸结构体)
    };

    class RawStructRegistry
    {
    public:
        using Snapshot = std::unordered_map<uint32_t, RawStructMeta>;

        static RawStructRegistry& instance()
        {
            static RawStructRegistry instance;
            return instance;
        }

        // 只在静态初始化期间调用。整张表重建一份再换上去，读者因此永远看
        // 到一个完整的、不可变的表。
        void registerStruct(uint32_t msg_index, std::string name, size_t size)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto                         next = std::make_shared<Snapshot>(*std::atomic_load(&snapshot_));
            (*next)[msg_index]                = RawStructMeta{std::move(name), size};
            std::atomic_store(&snapshot_, std::shared_ptr<const Snapshot>(std::move(next)));
        }

        // io 线程每帧调一次：读的是只读快照，不抢锁（ADR-0005 明确要求
        // 解码分派不能每包抢 mutex）。
        const RawStructMeta* find(uint32_t msg_index) const
        {
            const auto snap = std::atomic_load(&snapshot_);
            const auto it   = snap->find(msg_index);
            return it == snap->end() ? nullptr : &it->second;
        }

    private:
        RawStructRegistry() = default;

        mutable std::mutex              mutex_;
        std::shared_ptr<const Snapshot> snapshot_ = std::make_shared<const Snapshot>();
    };

    /**
     * @brief 裸结构体注册辅助类
     *
     * 两个 static_assert 是硬约束，不是提示：trivially copyable 保证可以按字节
     * 解释，alignof == 1 保证确实写了 pack(1)（没写的结构体 alignof 不会是 1）。
     */
    template <typename StructType>
    class RawStructRegistrar
    {
    public:
        RawStructRegistrar(uint32_t msg_index, std::string name) : msg_index_(msg_index)
        {
            static_assert(std::is_trivially_copyable<StructType>::value,
                          "裸结构体必须是 trivially copyable 的 POD");
            static_assert(alignof(StructType) == 1, "裸结构体必须 1 字节对齐：alignof(T) == 1 即 pack(1) 的证明");
            RawStructRegistry::instance().registerStruct(msg_index_, std::move(name), sizeof(StructType));
        }

    private:
        uint32_t msg_index_;
    };

    // ======== 业务分发注册表 ========
    //
    // 键是**消息名字**（字符串），不是整数 id —— v2 帧头删掉了 message_id_
    // （ADR-0011），整数那一套 MsgID 枚举随之废弃。
    class MessageHandlerRegistry
    {
    public:
        using MessageHandler = std::function<void(const NetworkMessage&)>;

        static MessageHandlerRegistry& instance()
        {
            static MessageHandlerRegistry instance;
            return instance;
        }

        void registerMessageHandler(std::string message_name, MessageHandler handler)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            handlers_[std::move(message_name)] = std::move(handler);
        }

        void unregisterMessageHandler(const std::string& message_name)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            handlers_.erase(message_name);
        }

        bool handleMessage(const NetworkMessage& message) const
        {
            const std::string message_name = message.messageName();

            std::unique_lock<std::mutex> lock(mutex_);
            auto                         it = handlers_.find(message_name);
            if (it != handlers_.end())
            {
                MessageHandler handler = it->second;
                lock.unlock();  // 业务回调不在锁里跑：它可能耗时、也可能回头注册别的 handler

                try
                {
                    handler(message);
                    return true;
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("Message handler exception for {}: {}", message_name, e.what());
                    return false;
                }
            }
            return false;
        }

        bool isRegistered(const std::string& message_name) const
        {
            std::unique_lock<std::mutex> lock(mutex_);
            return handlers_.find(message_name) != handlers_.end();
        }

    private:
        MessageHandlerRegistry()  = default;
        ~MessageHandlerRegistry() = default;

        MessageHandlerRegistry(const MessageHandlerRegistry&)            = delete;
        MessageHandlerRegistry& operator=(const MessageHandlerRegistry&) = delete;

        mutable std::mutex                                mutex_;
        std::unordered_map<std::string, MessageHandler>   handlers_;
    };

    /**
     * @brief 消息处理器注册辅助类（按消息名字注册）
     */
    class MessageHandlerRegistrar
    {
    public:
        explicit MessageHandlerRegistrar(std::string message_name, MessageHandlerRegistry::MessageHandler handler)
            : message_name_(std::move(message_name))
        {
            MessageHandlerRegistry::instance().registerMessageHandler(message_name_, std::move(handler));
        }

        ~MessageHandlerRegistrar()
        {
            MessageHandlerRegistry::instance().unregisterMessageHandler(message_name_);
        }

        MessageHandlerRegistrar(const MessageHandlerRegistrar&)            = delete;
        MessageHandlerRegistrar& operator=(const MessageHandlerRegistrar&) = delete;

    private:
        std::string message_name_;
    };

    /**
     * @brief 裸结构体注册宏
     *
     * 使用方式（结构体自己必须 #pragma pack(1)）：
     *     RAW_STRUCT(cncpp::msg_index::kAuth, cncpp::message_name::kAuth, cncpp::AuthPayload);
     *
     * 变量名靠 __COUNTER__ 而不是拿类型名去拼接：类型名可能是 `cncpp::AuthPayload`
     * 这种带限定符的，## 拼出来的不是合法标识符。
     */
#define RAW_STRUCT_CAT_IMPL(a, b) a##b
#define RAW_STRUCT_CAT(a, b)      RAW_STRUCT_CAT_IMPL(a, b)

#define RAW_STRUCT(MSG_INDEX, NAME, STRUCT_TYPE)                                                                  \
    static_assert(::std::is_trivially_copyable<STRUCT_TYPE>::value,                                               \
                  "裸结构体必须是 trivially copyable 的 POD");                                                     \
    static_assert(alignof(STRUCT_TYPE) == 1, "裸结构体必须 1 字节对齐（#pragma pack(1)）");                         \
    static ::cncpp::RawStructRegistrar<STRUCT_TYPE> RAW_STRUCT_CAT(raw_struct_registrar_, __COUNTER__)(MSG_INDEX,  \
                                                                                                      NAME)

}  // namespace cncpp

#endif  // MESSAGE_HANDLER_H

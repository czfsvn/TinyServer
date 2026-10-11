#ifndef MESSAGE_H
#define MESSAGE_H

#include <boost/asio.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>

#include "message_data.h"

using boost::asio::ip::tcp;

namespace cncpp
{
    // ======== 帧头（术语见 CONTEXT.md 的「帧」）========
    //
    // v2 线上布局，定长 12 字节，全部网络字节序（大端）：
    //   magic(4) | version(1) | format(1) | flags(2) | body_length(4)
    //
    // v1 末尾那个 message_id(4) 在 v2 里被删掉了：目标类型不再由帧头给出，
    // 改由 body 自带的类型标识给出（裸结构体帧的 msgindex、protobuf 帧的 msgname）。
    // 见 ADR-0011 与 CONTEXT.md 的「类型标识」。
    //
    // 内存里的 MessageHeader **不是**这个布局：它是主机字节序的普通结构体，
    // 只能通过 encodeHeader / decodeHeader 与字节流互转。
    // 不要对它 memcpy —— 那会把字节序和对齐假设重新引进来。

    constexpr uint32_t kMessageMagic   = 0x12345678;
    constexpr uint8_t  kMessageVersion = 2;
    constexpr size_t   kHeaderSize     = 12;

    // 单帧 body 的上限。必须在按 body_length_ 分配内存之前校验，
    // 否则一个伪造的头部就能让服务端分配 4GB。
    constexpr uint32_t kMaxBodyLength = 1024 * 1024;

    // ======== 类型标识（body 前缀，术语见 CONTEXT.md）========
    //
    // 它们是**协议字段**，与帧头同一套字节序规则（大端），不是载荷的一部分。
    // 类型标识是解码的输入；分发（交给哪个 handler）用的是消息名字——两者不是
    // 一回事，见 CONTEXT.md。

    constexpr size_t kRawIndexSize = 4;  // msgindex：body 前 4 字节，大端 uint32

    // msgname 的线上形态：body 开头的一个 **C 字符串** —— [名字字节…]['\0']，
    // 终止符之后才是 protobuf 载荷。
    //
    // 为什么不是定长 64 字节：protobuf 走的是高频路径，一个 20 字节的消息却要
    // 陪 64 字节的名字，是纯浪费（1000 连接 × 20 帧/秒 × 45 字节 ≈ 每秒 0.9 MB
    // 的纯填充）。
    //
    // 变长就必须还能**有界地**解析，所以这个常量有两重身份：既是名字长度上限，
    // 也是接收端的扫描上界 —— 前 kMaxMsgNameLength + 1 字节里找不到 '\0' 就是
    // 名字超长，丢弃而非截断（截断会让 cncpp.LoginRequest 变成 cncpp.Login 并
    // 静默解成另一个真实存在的类型）。
    constexpr size_t kMaxMsgNameLength = 63;

    // 载荷格式：body 里装的是什么。互斥取值，不是位标志（ADR-0006）。
    enum class PayloadFormat : uint8_t
    {
        kRawStruct   = 0,  // msgindex(4,大端) + 1 字节对齐的裸结构体
        kProtobuf    = 1,  // msgname(C 字符串) + 某个 protobuf 类型的序列化结果
        kFlatBuffers = 2,  // 只占枚举位，解码器未实现（ADR-0011）
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
        PayloadFormat format_      = PayloadFormat::kRawStruct;
        uint16_t      flags_       = 0;
        uint32_t      body_length_ = 0;
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

    // 裸结构体的字段字节序是**主机序（小端）**，不做转换——这是"强转"能成立的前提
    // （ADR-0011）。x86 与 ARM 都是小端。把它钉成编译期常量，将来真上大端机器时
    // 是编译失败，而不是运行期读到错值。
    inline constexpr bool kHostIsLittleEndian =
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
        (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);
#elif defined(_MSC_VER)
        true;  // MSVC 只跑在 x86 / ARM 上，恒为小端
#else
#error "无法在编译期判定主机字节序，裸结构体的主机序假设无法保证"
#endif

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
        // kFlatBuffers 也是合法值（解码器未实现，但帧头必须承认它，否则客户端
        // 连"这个格式服务端认得、只是暂不支持"和"协议不匹配"都分不清）。
        if (format > static_cast<uint8_t>(PayloadFormat::kFlatBuffers))
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
        return HeaderError::kOk;
    }

    // ======== 类型标识的编解码 ========
    //
    // 只负责 body 前缀那几个字节。失败一律返回 false，怎么处置由调用方按
    // ADR-0009（认得格式但解不开 → 丢弃 + 错误预算）决定。

    inline void appendMsgIndex(std::string& out, uint32_t msg_index)
    {
        unsigned char buf[kRawIndexSize];
        detail::storeBe32(buf, msg_index);
        out.append(reinterpret_cast<const char*>(buf), kRawIndexSize);
    }

    // 拼出裸结构体帧的完整 body：msgindex(4,大端) + 结构体字节。
    //
    // 调用方不该自己写 appendMsgIndex + reinterpret_cast 两连：前缀宽度与字节序
    // 是协议，漏一步就是畸形帧，而服务端在 io 线程按 ADR-0009 直接丢弃并计错误
    // 预算。收成一个函数之后，这两件事只有一处知道。
    template <class T>
    std::string makeRawBody(uint32_t msg_index, const T& value)
    {
        static_assert(std::is_trivially_copyable<T>::value, "裸结构体必须是 trivially copyable 的 POD");
        static_assert(alignof(T) == 1, "裸结构体必须 1 字节对齐：alignof(T) == 1 就是 pack(1) 的编译期证明");

        std::string body;
        appendMsgIndex(body, msg_index);
        body.append(reinterpret_cast<const char*>(&value), sizeof(T));
        return body;
    }

    inline bool readMsgIndex(const std::string& body, uint32_t& out)
    {
        if (body.size() < kRawIndexSize)
        {
            return false;
        }
        out = detail::loadBe32(reinterpret_cast<const unsigned char*>(body.data()));
        return true;
    }

    inline bool appendMsgName(std::string& out, const std::string& msg_name)
    {
        if (msg_name.empty() || msg_name.size() > kMaxMsgNameLength)
        {
            return false;
        }
        out.append(msg_name);
        out.push_back('\0');
        return true;
    }

    // 读出 msgname 并告诉调用方**载荷从哪里开始**。
    //
    // payload_offset 必须回传，不能让调用方自己算 name.size() + 1：'\0' 是消失
    // 在线上格式里的一个字节，凡是需要它的人都漏得掉。
    //
    // 失败一律返回 false，怎么处置由调用方按 ADR-0009 决定。
    inline bool readMsgName(const std::string& body, std::string& out, size_t& payload_offset)
    {
        // 扫描必须有界。body 最坏有 kMaxBodyLength(1MB)，而这跑在 io 线程上、
        // 每帧一次；名字超长时前半段本来就没有 '\0'，不设上界就是一条畸形帧
        // 让 io 线程线性扫 1MB —— 正是 ADR-0005 说的"解码必须有界"。
        const size_t scan_size  = body.size() < kMaxMsgNameLength + 1 ? body.size() : kMaxMsgNameLength + 1;
        const void*  terminator = std::memchr(body.data(), '\0', scan_size);
        if (!terminator)
        {
            return false;  // 名字超长（或压根没有终止符），丢弃而非截断
        }

        const size_t name_length = static_cast<const char*>(terminator) - body.data();
        if (name_length == 0)
        {
            return false;  // 空名字，与 appendMsgName 拒绝空的规则对称
        }

        out.assign(body.data(), name_length);
        payload_offset = name_length + 1;  // 终止符那一个字节也算在名字字段里
        return true;
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

        // 载荷格式。**帧头的 format_ 是唯一真相**（ADR-0011）：data_ 不自报格式，
        // 业务侧先问这个再决定取用方式。
        PayloadFormat format() const
        {
            return header_.format_;
        }

        // 解码得出的消息名字：**业务分发的键**。protobuf 帧就是它的 msgname，
        // 裸结构体帧由注册表从 msgindex 反查而来。它与类型标识不是一回事
        // （见 CONTEXT.md）。
        std::string messageName() const
        {
            return data_ ? data_->typeName() : std::string();
        }

        // 裸结构体帧的 msgindex。非裸结构体帧返回 0。
        uint32_t msgIndex() const
        {
            return data_ ? data_->typeIndex() : 0;
        }

        // 完整 body 字节（含类型标识前缀）。裸结构体帧要读结构体请用 as<T>()。
        const std::string* getBody() const
        {
            return data_ ? data_->getBody() : nullptr;
        }

        /**
         * @brief 把裸结构体的字节直接解释成 T 的视图（零拷贝强转）
         *
         * 三重校验，缺一不可：
         *   1. 编译期：T 必须 trivially copyable；
         *   2. 编译期：alignof(T) == 1 —— 这就是"确实写了 pack(1)"的证明，
         *      忘写 pragma 的结构体在这里编译失败，不用靠人肉检查；
         *   3. 运行期：body 长度必须正好等于 kRawIndexSize + sizeof(T)。
         *
         * 返回的指针只在 IMessageData 存活期内有效，不要存下来。
         */
        template <class T>
        const T* as() const
        {
            static_assert(std::is_trivially_copyable<T>::value, "裸结构体必须是 trivially copyable 的 POD");
            static_assert(alignof(T) == 1, "裸结构体必须 1 字节对齐：alignof(T) == 1 就是 pack(1) 的编译期证明");
            static_assert(kHostIsLittleEndian, "裸结构体字段是主机序小端，大端平台上必须逐字段转换（ADR-0011）");

            const std::string* body = getBody();
            if (!body || header_.format_ != PayloadFormat::kRawStruct)
            {
                return nullptr;
            }
            if (body->size() != kRawIndexSize + sizeof(T))
            {
                return nullptr;
            }
            return reinterpret_cast<const T*>(body->data() + kRawIndexSize);
        }

        const google::protobuf::Message* getProtobuf() const
        {
            return data_ ? data_->getProtobuf() : nullptr;
        }

        google::protobuf::Message* getMutableProtobuf()
        {
            return data_ ? data_->getMutableProtobuf() : nullptr;
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

        // 裸结构体：body 必须已经是 "msgindex(4) + 结构体字节" 的完整形态。
        // 前缀不在这里拼——拼填充与字节序是 Session 发送路径的事（ADR-0011）。
        void setRaw(uint32_t msg_index, std::string type_name, std::string body)
        {
            header_.format_ = PayloadFormat::kRawStruct;
            data_           = cncpp::createRawStructMessage(msg_index, std::move(type_name), std::move(body));
        }

        // 直接给结构体对象的便利重载：前缀在这里拼一次，调用方不碰编码规则。
        template <class T>
        void setRaw(uint32_t msg_index, std::string type_name, const T& value)
        {
            // 前缀与字节序交给 makeRawBody，那份 static_assert 也在那里
            setRaw(msg_index, std::move(type_name), makeRawBody(msg_index, value));
        }

        void setProtobuf(std::string msg_name, std::shared_ptr<google::protobuf::Message> proto)
        {
            header_.format_ = PayloadFormat::kProtobuf;
            data_           = cncpp::createProtobufMessage(std::move(msg_name), std::move(proto));
        }

        static NetworkMessage createRawMessage(const MessageHeader& header, uint32_t msg_index, std::string type_name,
                                              std::string body, const tcp::endpoint& sender)
        {
            NetworkMessage msg(header, sender);
            msg.setRaw(msg_index, std::move(type_name), std::move(body));
            return msg;
        }

        static NetworkMessage createProtobufMessage(const MessageHeader&                       header,
                                                    std::string                                msg_name,
                                                    std::shared_ptr<google::protobuf::Message> proto,
                                                    const tcp::endpoint&                       sender)
        {
            NetworkMessage msg(header, sender);
            msg.setProtobuf(std::move(msg_name), std::move(proto));
            return msg;
        }

        // ======== 反射相关方法 ========

        /**
         * @brief 获取消息类型名称
         * @return 消息类型名称，如果不是protobuf消息返回空字符串
         */
        std::string getTypeName() const
        {
            return messageName();
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

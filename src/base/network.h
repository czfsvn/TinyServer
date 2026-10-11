#ifndef NETWORK_CONNECTION_H
#define NETWORK_CONNECTION_H

#include <array>
#include <atomic>
#include <boost/asio.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "compression.h"
#include "encryption.h"
#include "logger.h"
#include "message.h"
#include "my_concurrent_queue.h"
#include "protobuf_util.h"

using boost::asio::ip::tcp;

namespace cncpp
{
    class Session : public std::enable_shared_from_this<Session>
    {
    public:
        Session(tcp::socket socket);
        ~Session();

        void start();

        // ======== 发送 ========
        //
        // body 前缀（裸结构体的 4 字节大端 msgindex、protobuf 的 64 字节 '\0'
        // 填充 msgname）**只在这几个方法里拼一次**。让调用方自己拼，必然有人漏
        // 掉填充或写错字节序——而那两种错误都不会有编译错误，只在对端表现为
        // "解不出来"（ADR-0011）。

        // 裸结构体：msgindex + 一段已经是 pack(1) 的字节
        void sendRawStruct(uint32_t msg_index, const void* struct_bytes, size_t size);

        template <class T>
        void sendRawStruct(uint32_t msg_index, const T& value)
        {
            static_assert(std::is_trivially_copyable<T>::value, "裸结构体必须是 trivially copyable 的 POD");
            static_assert(alignof(T) == 1, "裸结构体必须 1 字节对齐：alignof(T) == 1 即 pack(1) 的证明");
            sendRawStruct(msg_index, &value, sizeof(T));
        }

        // ======== protobuf 发送 ========
        template <class PROTO>
        static std::shared_ptr<const std::string> makeSharedProtobufPayload(const PROTO& message)
        {
            static_assert(std::is_base_of<google::protobuf::Message, PROTO>::value,
                          "makeSharedProtobufPayload 只接受 google::protobuf::Message 及其派生类型");

            std::string body;
            const auto& msg_name = message.GetDescriptor()->full_name();
            if (!appendMsgName(body, msg_name))
            {
                LOG_ERROR("Dropping outgoing protobuf message: invalid msgname '{}' (empty or longer than {})",
                          msg_name, kMaxMsgNameLength);
                return nullptr;
            }
            body.append(ProtobufUtil::Serialize(message));
            return std::make_shared<const std::string>(std::move(body));
        }

        template <class PROTO>
        void sendProtoMsg(const PROTO& message)
        {
            static_assert(std::is_base_of<google::protobuf::Message, PROTO>::value,
                          "sendProtoMsg 只接受 google::protobuf::Message 及其派生类型");

            // 单播走这里是"每个 Session 序列化一次"。广播别逐个调这个，
            // 而是 makeSharedProtobufPayload() 序列化一次、把同一份 shared_ptr 发给所有人。
            if (auto payload = makeSharedProtobufPayload(message))
            {
                send(std::move(payload), PayloadFormat::kProtobuf);
            }
        }

        // 沿用入站帧的类型标识回包（一问一答用）。刻意用**入站帧**的名字而不是
        // 回包对象自己的类型名：客户端靠这个把请求与响应对上。广播与主动推送
        // 没有入站帧，请用 sendProtoMsg / sendRawStruct。
        void replyRawStruct(const NetworkMessage& request, const void* struct_bytes, size_t size);

        // 零拷贝发送：载荷已在调用方手里，入队时不再分配、不再复制。
        // 广播时用 makeShared*Payload() 只序列化一次，把同一份 shared_ptr 交给
        // 所有接收者 —— 这是"广播 N 人"从 N 次序列化 + N 份拷贝降为 1 次的入口。
        // 传入的 body 必须是**含类型标识前缀**的完整形态。
        void send(std::shared_ptr<const std::string> body, PayloadFormat format);

        // 序列化一次，产出可广播共享的载荷（已含类型标识前缀）。
        //
        // 刻意不提供"延迟到 io 线程再序列化"的版本：那会把 protobuf 对象的生命
        // 周期拖到 io 线程上，业务侧"复用同一个 msg 对象连发两次"的写法会静默
        // 出错（两条都看到最后一次的字段值），且只在特定时序下偶发。
        static std::shared_ptr<const std::string> makeSharedRawPayload(uint32_t msg_index, const void* struct_bytes,
                                                                       size_t size);
        // 调用方已经拼好完整 body（含前缀）时用这个
        static std::shared_ptr<const std::string> makeSharedPayload(std::string body);

        void setEncryption(std::shared_ptr<EncryptionInterface> encryption);
        void setCompression(std::shared_ptr<CompressionInterface> compression);
        void close();
        /**
         * @brief 检查会话是否处于打开状态
         * @return 是否打开
         */
        bool          isOpen() const;
        tcp::endpoint getRemoteEndpoint() const;

        // 获取接收队列。返回的队列不是线程通用的：只能由 io 线程 tryPush、tick 线程 tryPop。
        MyConcurrentQueue<NetworkMessage>& getReceiveQueue();

    private:
        void doReadHeader();
        void doReadBody();
        // 拆帧的产物交到这里解码。返回 false 表示应当关闭连接。
        bool processMessage(std::string body, const MessageHeader& header);
        void processSendQueue();

        // start()/send()/close() 可能被 accept 回调线程或 tick 线程调用，
        // 而对 socket_ 的任何操作（发起 async_read/async_write、shutdown、close）
        // 都必须发生在本 Session 所属的 io_context 线程上 —— 单线程 io_context
        // 的串行保证是"每个 context 挂多个 Session"这个模型唯一的地基，
        // 跨线程直接操作会把地基抽掉。下面三个私有方法就是为此存在的：
        //   doStart()/doClose() 是真正的实现，postToContext() 负责把它们送回本 context。
        void doStart();
        void doClose();
        void postToContext(std::function<void()> fn);

        // 连续解码失败的次数，成功解码一帧即清零（ADR-0009）。
        // 只在 io 线程上读写，不需要同步。
        static constexpr uint32_t kMaxConsecutiveDecodeFailures = 10;

        tcp::socket                           socket_;
        std::size_t                           total_bytes_read_;
        std::size_t                           total_bytes_written_;
        std::shared_ptr<EncryptionInterface>  encryption_;
        std::shared_ptr<CompressionInterface> compression_;

        // 发送消息队列 (body, format)
        //
        // body 是 shared_ptr<const string> 而不是 unique_ptr<string>：广播时 N 个
        // Session 共享同一份字节，序列化与内存分配都只发生一次。
        // const 是这里的要害 —— 同一份载荷会被多个 io 线程并发读（各 Session 分
        // 布在不同 context 上），只要有一处可写，"只读共享"就立刻变成数据竞争。
        //
        // 队列里不再带 message_id：v2 帧头删掉了它（ADR-0011），类型标识已在
        // body 前缀里。
        using SendQueueItem = std::tuple<std::shared_ptr<const std::string>, PayloadFormat>;

        // 用于读取消息：线上字节先进 header_buffer_，解码后才成为 current_header_
        std::array<unsigned char, kHeaderSize> header_buffer_;
        MessageHeader                          current_header_;
        std::vector<char>                      body_buffer_;

        // 连接建立时缓存一次。拆帧路径上每帧都要拿它构造 NetworkMessage，
        // 而 remote_endpoint() 的抛异常版本在 socket 已关时会抛，不能留在 io handler 里。
        tcp::endpoint remote_endpoint_;

        uint32_t consecutive_decode_failures_ = 0;

        // 因接收队列到上限而丢弃的消息数。每条丢失的消息在这里都有痕迹，
        // 不会被记作"网络没发过来"。
        uint32_t queue_full_drops_ = 0;

        // 因发送队列到上限而丢弃的消息数。与接收侧分开计：上行堵和下行堵的处置
        // 完全不同（前者是业务降级，后者会让客户端状态永久不一致），混在一个数
        // 里，出问题时分不清是哪一侧。
        uint32_t send_queue_full_drops_ = 0;

        // 发送队列。真多生产者（tick 线程 + 任意业务线程）、单消费者（本 Session
        // 所属的 io 线程）—— 这是引入无锁队列唯一有实质收益的地方：早先这里是一把
        // std::mutex 加 std::queue（ADR-0010）。
        MyConcurrentQueue<SendQueueItem> send_queue_;

        // 是否已有写链在跑。它不是个普通标志位，而是 kick 握手的一半，另一半在
        // processSendQueue() 里 —— 改动前先读那边的注释。早先它靠 send_queue_mutex_
        // 与"取空"关在同一个临界区里，换成无锁队列后临界区没了，改成双检模式；
        // 少任何一半都会出现"消息入了队但再也没人发"的永久静默（lost wakeup）。
        std::atomic<bool> is_sending_;

        // 接收队列：io 线程投递，tick 线程取走
        MyConcurrentQueue<NetworkMessage> receive_queue_;

        // 本 Session 所属的 executor（构造时从 socket 取出并缓存）。
        // socket 是被 async_accept(target_ctx, ...) / connect 建在目标 io_context 上的，
        // 所以这里拿到的就是它真正所属的那个 io_context 的 executor。
        //
        // 存 executor 而不是向下转 io_context*：get_executor().context() 给的是基类
        // execution_context&，而 Boost 1.85 的 execution_context 不是多态类型，
        // dynamic_cast 编不过，static_cast 又太脆。
        boost::asio::any_io_executor executor_;
    };

    class Acceptor : public std::enable_shared_from_this<Acceptor>
    {
    public:
        using ConnectionCallback = std::function<void(tcp::socket&& sock)>;

        // 为每条新连接挑选承载它的 io_context。
        // 注入进来而不是让 network 直接依赖 IOContextPool，保持分层；
        // 传 nullptr 则退回 asio 默认行为（新 socket 建在 acceptor 自己的 context 上）。
        using ContextPicker = std::function<boost::asio::io_context&()>;

        Acceptor(boost::asio::io_context& io_context, short port, ConnectionCallback connection_callback,
                 ContextPicker session_ctx_picker = nullptr);

        void start();
        void stop();

        bool isRunning() const;

    private:
        std::atomic<bool> is_running_{false};

    private:
        void doAccept();

        tcp::acceptor      acceptor_;
        ConnectionCallback connection_callback_;
        ContextPicker      session_ctx_picker_;
    };

    class Connector : public std::enable_shared_from_this<Connector>
    {
    public:
        using ConnectCallback = std::function<void(tcp::socket&& sock)>;
        using ErrorCallback   = std::function<void(const std::string&)>;

        Connector(boost::asio::io_context& io_context);

        void connect(const std::string& host, short port, ConnectCallback connect_callback,
                     ErrorCallback error_callback);

    private:
        boost::asio::io_context& io_context_;
        tcp::socket              socket_;
        ConnectCallback          connect_callback_;
        ErrorCallback            error_callback_;
    };
}  // namespace cncpp
#endif  // NETWORK_CONNECTION_H

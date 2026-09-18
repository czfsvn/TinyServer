#ifndef NETWORK_CONNECTION_H
#define NETWORK_CONNECTION_H

#include <array>
#include <atomic>
#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <tuple>
#include <vector>

#include "compression.h"
#include "spsc_message_queue.h"
#include "encryption.h"
#include "logger.h"
#include "message.h"
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

        // 以 kRaw 格式发送不透明字节
        void send(const std::string& body, uint32_t message_id = 0);
        // 以 kProtobuf 格式发送，body 是 message 的序列化结果
        void send(const google::protobuf::Message& message, uint32_t message_id = 0);
        // 显式指定载荷格式。压缩与加密标志由 doSend 自行置位，不需要调用方传。
        void send(const std::string& body, uint32_t message_id, PayloadFormat format);

        void setEncryption(std::shared_ptr<EncryptionInterface> encryption);
        void setCompression(std::shared_ptr<CompressionInterface> compression);
        void close();
        /**
         * @brief 检查会话是否处于打开状态
         * @return 是否打开
         */
        bool          isOpen() const;
        tcp::endpoint getRemoteEndpoint() const;

        // 获取接收队列。返回的队列不是线程通用的：只能由 io 线程 push、tick 线程 pop。
        SpscMessageQueue& getReceiveQueue();

    private:
        void doSend(std::string body, uint32_t message_id, PayloadFormat format);
        void doReadHeader();
        void doReadBody();
        // 拆帧的产物交到这里解码。返回 false 表示应当关闭连接。
        bool processMessage(std::string body, const MessageHeader& header);
        void processSendQueue();

        // 连续解码失败的次数，成功解码一帧即清零（ADR-0009）。
        // 只在 io 线程上读写，不需要同步。
        static constexpr uint32_t kMaxConsecutiveDecodeFailures = 10;

        tcp::socket                           socket_;
        std::size_t                           total_bytes_read_;
        std::size_t                           total_bytes_written_;
        std::shared_ptr<EncryptionInterface>  encryption_;
        std::shared_ptr<CompressionInterface> compression_;

        // 发送消息队列 (body, message_id, format)
        using SendQueueItem = std::tuple<std::unique_ptr<std::string>, uint32_t, PayloadFormat>;

        // 用于读取消息：线上字节先进 header_buffer_，解码后才成为 current_header_
        std::array<unsigned char, kHeaderSize> header_buffer_;
        MessageHeader                          current_header_;
        std::vector<char>                      body_buffer_;

        // 连接建立时缓存一次。拆帧路径上每帧都要拿它构造 NetworkMessage，
        // 而 remote_endpoint() 的抛异常版本在 socket 已关时会抛，不能留在 io handler 里。
        tcp::endpoint remote_endpoint_;

        uint32_t consecutive_decode_failures_ = 0;

        // 因接收队列满而丢弃的消息数。每条丢失的消息在这里都有痕迹，
        // 不会被记作"网络没发过来"。
        uint32_t queue_full_drops_ = 0;

        std::queue<SendQueueItem> send_queue_;
        std::mutex                send_queue_mutex_;
        bool                      is_sending_;

        // 接收队列：io 线程投递，tick 线程取走
        SpscMessageQueue receive_queue_;
    };

    class Acceptor : public std::enable_shared_from_this<Acceptor>
    {
    public:
        using ConnectionCallback = std::function<void(tcp::socket&& sock)>;

        Acceptor(boost::asio::io_context& io_context, short port, ConnectionCallback connection_callback);

        void start();
        void stop();

        bool isRunning() const;

    private:
        std::atomic<bool> is_running_{false};

    private:
        void doAccept();

        tcp::acceptor      acceptor_;
        ConnectionCallback connection_callback_;
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

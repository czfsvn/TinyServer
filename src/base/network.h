#ifndef NETWORK_CONNECTION_H
#define NETWORK_CONNECTION_H

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
#include "dual_lock_free_queue.h"
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
        void send(const std::string& body, uint32_t message_id = 0);
        void send(const google::protobuf::Message& message, uint32_t message_id = 0);
        void send(const std::string& body, uint32_t message_id, uint32_t additional_flags);
        void setEncryption(std::shared_ptr<EncryptionInterface> encryption);
        void setCompression(std::shared_ptr<CompressionInterface> compression);
        void close();
        /**
         * @brief 检查会话是否处于打开状态
         * @return 是否打开
         */
        bool          isOpen() const;
        tcp::endpoint getRemoteEndpoint() const;

        // 获取接收消息队列
        DualLockFreeQueue& getReceiveQueue();

    private:
        void doSend(std::string body, uint32_t message_id);
        void doSend(std::string body, uint32_t message_id, uint32_t additional_flags);
        void doReadHeader();
        void doReadBody();
        void processMessage(std::string body, MessageHeader header);
        void processSendQueue();

        tcp::socket                           socket_;
        static const int                      kMaxLength = 4096;
        std::size_t                           total_bytes_read_;
        std::size_t                           total_bytes_written_;
        std::shared_ptr<EncryptionInterface>  encryption_;
        std::shared_ptr<CompressionInterface> compression_;
        uint32_t                              current_message_id_;

        // 发送消息队列 (body, message_id, additional_flags)
        using SendQueueItem = std::tuple<std::unique_ptr<std::string>, uint32_t, uint32_t>;

        // 用于读取消息
        MessageHeader     current_header_;
        std::vector<char> body_buffer_;

        std::queue<SendQueueItem> send_queue_;
        std::mutex                send_queue_mutex_;
        bool                      is_sending_;

        // 接收消息队列（双无锁队列）
        DualLockFreeQueue receive_queue_;
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

#include "network.h"
#include <cstring>

#include "config.h"
#include "message_handler.h"

namespace cncpp
{

    // Session 类实现
    Session::Session(tcp::socket socket)
        : socket_(std::move(socket)),
          total_bytes_read_(0),
          total_bytes_written_(0),
          encryption_(nullptr),
          compression_(nullptr),
          is_sending_(false)
    {
    }

    Session::~Session()
    {
        close();
    }

    void Session::start()
    {
        boost::system::error_code ec;
        remote_endpoint_ = socket_.remote_endpoint(ec);
        if (ec)
        {
            LOG_WARN("Cannot resolve remote endpoint: {}", ec.message());
        }

        doReadHeader();
    }

    void Session::send(const std::string& body, uint32_t message_id)
    {
        send(body, message_id, PayloadFormat::kRaw);
    }

    void Session::send(const google::protobuf::Message& message, uint32_t message_id)
    {
        std::string serialized_message = ProtobufUtil::Serialize(message);
        if (!serialized_message.empty())
        {
            send(serialized_message, message_id, PayloadFormat::kProtobuf);
        }
    }

    void Session::send(const std::string& body, uint32_t message_id, PayloadFormat format)
    {
        std::unique_lock<std::mutex> lock(send_queue_mutex_);
        send_queue_.push(std::make_tuple(std::make_unique<std::string>(body), message_id, format));

        // 如果当前没有正在发送的消息，启动发送流程
        if (!is_sending_)
        {
            is_sending_ = true;
            lock.unlock();
            processSendQueue();
        }
    }

    void Session::setEncryption(std::shared_ptr<EncryptionInterface> encryption)
    {
        encryption_ = encryption;
    }

    void Session::setCompression(std::shared_ptr<CompressionInterface> compression)
    {
        compression_ = compression;
    }

    void Session::close()
    {
        // 这里不"停"接收队列：队列里已经解码好的帧应当继续被 tick 线程取走。
        // 队列只负责搬运，连接关闭之后由谁停止消费是 SessionManager 的事（见 A5）。
        // 早先这里靠 receive_queue_.stop() 让 pop 在空队列上返回 true，
        // 而消费端一律写 while (pop(message))，于是队列一停就变成死循环。

        // 先发 FIN，再关闭句柄。顺序反过来会让对端收到 RST 而不是正常的 EOF，
        // 对端就可能把已经发出的数据当成丢失。
        boost::system::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        if (ec && ec != boost::asio::error::not_connected && ec != boost::asio::error::bad_descriptor)
        {
            LOG_WARN("Shutdown error: {}", ec.message());
        }

        socket_.close(ec);
        if (ec)
        {
            LOG_ERROR("Close error: {}", ec.message());
        }
    }

    bool Session::isOpen() const
    {
        return socket_.is_open();
    }

    tcp::endpoint Session::getRemoteEndpoint() const
    {
        return remote_endpoint_;
    }

    // 获取接收队列
    SpscMessageQueue& Session::getReceiveQueue()
    {
        return receive_queue_;
    }

    void Session::processSendQueue()
    {
        SendQueueItem message;

        {
            std::unique_lock<std::mutex> lock(send_queue_mutex_);
            if (!send_queue_.empty())
            {
                message = std::move(send_queue_.front());
                send_queue_.pop();
            }
            else
            {
                is_sending_ = false;
                return;
            }
        }

        // 处理消息发送
        doSend(std::move(*std::get<0>(message)), std::get<1>(message), std::get<2>(message));
    }

    void Session::doSend(std::string body, uint32_t message_id, PayloadFormat format)
    {
        auto self(shared_from_this());

        uint16_t flags = 0;

        // 压缩
        if (compression_)
        {
            body  = compression_->Compress(body);
            flags = static_cast<uint16_t>(flags | kFlagCompressed);
        }

        // 加密
        if (encryption_)
        {
            body  = encryption_->Encrypt(body);
            flags = static_cast<uint16_t>(flags | kFlagEncrypted);
        }

        if (body.size() > kMaxBodyLength)
        {
            LOG_ERROR("Dropping outgoing message_id={}: body {} exceeds limit {}", message_id, body.size(),
                      kMaxBodyLength);
            processSendQueue();
            return;
        }

        // 构建帧头（format 是载荷语义，flags 只是传输层变换，两者正交）
        MessageHeader header;
        header.format_      = format;
        header.flags_       = flags;
        header.body_length_ = static_cast<uint32_t>(body.size());
        header.message_id_  = message_id;

        // 完整帧 = kHeaderSize 字节定长帧头 + body。
        // buffer 必须是堆上共享的：async_write 只是持有这块内存的引用，
        // 而 doSend 在发起后就返回了，局部变量会在写入完成前析构。
        auto buffer = std::make_shared<std::vector<char>>(kHeaderSize + body.size());
        encodeHeader(header, buffer->data());
        if (!body.empty())
        {
            std::memcpy(buffer->data() + kHeaderSize, body.data(), body.size());
        }

        boost::asio::async_write(socket_, boost::asio::buffer(*buffer),
                                 [this, self, buffer](boost::system::error_code ec, std::size_t length) {
            if (!ec)
            {
                total_bytes_written_ += length;
            }
            else
            {
                LOG_ERROR("Send error: {}", ec.message());
            }

            // 继续处理下一条消息
            processSendQueue();
        });
    }

    void Session::doReadHeader()
    {
        auto self(shared_from_this());
        boost::asio::async_read(socket_, boost::asio::buffer(header_buffer_),
                                [this, self](boost::system::error_code ec, std::size_t length) {
            if (ec)
            {
                if (ec != boost::asio::error::operation_aborted)
                {
                    LOG_ERROR("Read header error: {}", ec.message());
                }
                close();
                return;
            }

            total_bytes_read_ += length;

            // 拆帧：线上字节 -> 帧头。校验顺序 magic -> version -> format -> body_length。
            const HeaderError err = decodeHeader(header_buffer_.data(), current_header_);
            if (err != HeaderError::kOk)
            {
                // body_length 本身就在帧头里，头部不合格就无从知道该跳过多少字节，
                // 帧同步已经丢了，唯一能做的是关连接。绝不重试读下一帧。
                LOG_ERROR("Bad frame header, closing connection: {}", toString(err));
                close();
                return;
            }

            if (current_header_.body_length_ > 0)
            {
                body_buffer_.resize(current_header_.body_length_);
                doReadBody();
            }
            else if (processMessage(std::string(), current_header_))
            {
                // 空 body 的帧，解码成功后继续读下一帧
                doReadHeader();
            }
            else
            {
                close();
            }
        });
    }

    void Session::doReadBody()
    {
        auto self(shared_from_this());
        boost::asio::async_read(socket_, boost::asio::buffer(body_buffer_),
                                [this, self](boost::system::error_code ec, std::size_t length) {
            if (ec)
            {
                if (ec != boost::asio::error::operation_aborted)
                {
                    LOG_ERROR("Read body error: {}", ec.message());
                }
                close();
                return;
            }

            total_bytes_read_ += length;

            // 拆帧完成，把 body 交给解码这一步（ADR-0008）
            const bool keep_open
                = processMessage(std::string(body_buffer_.begin(), body_buffer_.end()), current_header_);

            if (keep_open)
            {
                doReadHeader();
            }
            else
            {
                close();
            }
        });
    }

    bool Session::processMessage(std::string body, const MessageHeader& header)
    {
        // 传输层变换：发送侧是「先压缩再加密」，这里必须严格反序还原
        if ((header.flags_ & kFlagEncrypted) && encryption_)
        {
            body = encryption_->Decrypt(body);
        }

        if ((header.flags_ & kFlagCompressed) && compression_)
        {
            body = compression_->Decompress(body);
        }

        // 投递到接收队列。队列满时丢弃，这是背压而不是错误：tick 这一拍没跟上，
        // 让 io 线程等它只会把慢扩散到所有连接上。每 1024 条才报一次警，
        // 否则持续过载会先于真正的问题把 io 线程淹死在日志里。
        auto enqueue = [this](NetworkMessage&& message) {
            if (receive_queue_.push(std::move(message)))
            {
                return;
            }

            if (++queue_full_drops_ % 1024 == 1)
            {
                LOG_WARN("Receive queue full, dropped {} messages on this session", queue_full_drops_);
            }
        };

        if (header.format_ == PayloadFormat::kRaw)
        {
            enqueue(NetworkMessage::createTextMessage(header, std::move(body), remote_endpoint_));
            return true;
        }
        else if (header.format_ == PayloadFormat::kProtobuf)
        {
            auto proto_message = MessageHandlerRegistry::instance().createMessage(header.message_id_);
            if (!proto_message)
            {
                // 服务端不认得这个 id：丢弃这一帧，连接保持。这是服务端缺注册，
                // 不是客户端发了坏包，因此不计入错误预算（ADR-0007）。
                LOG_WARN("No handler registered for message_id={}, dropping frame", header.message_id_);
                return true;
            }

            if (!ProtobufUtil::Deserialize(body, *proto_message))
            {
                // 认得 id 却解不开：丢弃这一帧、连接保持，连续超阈值才关（ADR-0009）。
                // 帧是长度前缀的，丢弃不会让流失同步，所以这里是策略选择而非正确性要求。
                ++consecutive_decode_failures_;
                LOG_ERROR("Failed to decode message_id={} ({} consecutive), dropping frame", header.message_id_,
                          consecutive_decode_failures_);
                return consecutive_decode_failures_ < kMaxConsecutiveDecodeFailures;
            }

            consecutive_decode_failures_ = 0;
            enqueue(NetworkMessage::createProtobufMessage(header, std::move(proto_message), remote_endpoint_));
            return true;
        }
    }

    // Acceptor 类实现
    Acceptor::Acceptor(boost::asio::io_context& io_context, short port, ConnectionCallback connection_callback)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), connection_callback_(connection_callback)
    {
    }

    void Acceptor::start()
    {
        is_running_ = true;
        doAccept();
    }

    void Acceptor::stop()
    {
        // 使用原子操作确保线程安全
        bool expected = true;
        if (!is_running_.compare_exchange_strong(expected, false))
        {
            return;  // 已经停止
        }

        boost::system::error_code ec;
        acceptor_.close(ec);
        if (ec)
        {
            LOG_ERROR("Acceptor stop error: {}", ec.message());
        }
    }

    void Acceptor::doAccept()
    {
        if (!isRunning())
        {
            LOG_DEBUG("Acceptor is stopped, skipping async_accept");
            return;
        }

        auto self(shared_from_this());
        auto async_accept_handler = [this, self](boost::system::error_code ec, tcp::socket socket) {
            if (!ec)
            {
                if (connection_callback_)
                {
                    connection_callback_(std::move(socket));
                }
            }
            else
            {
                // 只有在acceptor仍在运行时才记录错误
                if (self->isRunning())
                {
                    LOG_ERROR("Accept error: {}", ec.message());
                }
                else
                {
                    LOG_DEBUG("Accept error after stop: {}", ec.message());
                }
            }

            // 只有在acceptor仍在运行时才继续接受连接
            if (self->isRunning())
            {
                self->doAccept();
            }
        };

        acceptor_.async_accept(async_accept_handler);
    }

    bool Acceptor::isRunning() const
    {
        return is_running_;
    }

    // Connector 类实现
    Connector::Connector(boost::asio::io_context& io_context) : io_context_(io_context), socket_(io_context)
    {
    }

    void Connector::connect(const std::string& host, short port, ConnectCallback connect_callback,
                            ErrorCallback error_callback)
    {
        connect_callback_ = connect_callback;
        error_callback_   = error_callback;

        tcp::resolver resolver(io_context_);
        auto          endpoints = resolver.resolve(host, std::to_string(port));

        auto self(shared_from_this());
        auto async_connect_handler = [this, self](boost::system::error_code ec, tcp::endpoint endpoint) {
            (void)endpoint;  // 显式标记为未使用
            if (!ec)
            {
                if (connect_callback_)
                {
                    connect_callback_(std::move(socket_));
                }
            }
            else
            {
                std::string error_msg = "Connect error: " + ec.message();
                LOG_ERROR(error_msg);

                if (error_callback_)
                {
                    error_callback_(error_msg);
                }
            }
        };

        boost::asio::async_connect(socket_, endpoints, async_connect_handler);
    }
}  // namespace cncpp
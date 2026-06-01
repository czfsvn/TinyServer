#include "network.h"
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
          current_message_id_(0),
          is_sending_(false)
    {
    }

    Session::~Session()
    {
        close();
    }

    void Session::start()
    {
        doReadHeader();
    }

    void Session::send(const std::string& body, uint32_t message_id)
    {
        std::unique_lock<std::mutex> lock(send_queue_mutex_);
        send_queue_.push(std::make_tuple(std::make_unique<std::string>(body), message_id, static_cast<uint32_t>(0)));

        // 如果当前没有正在发送的消息，启动发送流程
        if (!is_sending_)
        {
            is_sending_ = true;
            lock.unlock();
            processSendQueue();
        }
    }

    void Session::send(const google::protobuf::Message& message, uint32_t message_id)
    {
        std::string serialized_message = ProtobufUtil::Serialize(message);
        if (!serialized_message.empty())
        {
            // 设置protobuf标志位（第3位）
            uint32_t additional_flags = 0x04;  // protobuf格式标志
            send(serialized_message, message_id, additional_flags);
        }
    }

    void Session::send(const std::string& body, uint32_t message_id, uint32_t additional_flags)
    {
        std::unique_lock<std::mutex> lock(send_queue_mutex_);
        send_queue_.push(std::make_tuple(std::make_unique<std::string>(body), message_id, additional_flags));

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
        // 停止接收队列
        receive_queue_.stop();

        // 关闭socket
        boost::system::error_code ec;
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
        return socket_.remote_endpoint();
    }

    // 获取接收消息队列
    DualLockFreeQueue& Session::getReceiveQueue()
    {
        return receive_queue_;
    }

    void Session::processSendQueue()
    {
        std::tuple<std::unique_ptr<std::string>, uint32_t, uint32_t> message;

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

    void Session::doSend(std::string body, uint32_t message_id, uint32_t additional_flags)
    {
        auto self(shared_from_this());

        uint32_t flags = additional_flags;

        // 压缩
        if (compression_)
        {
            body = compression_->Compress(body);
            flags |= 0x01;  // 设置压缩标志
        }

        // 加密
        if (encryption_)
        {
            body = encryption_->Encrypt(body);
            flags |= 0x02;  // 设置加密标志
        }

        // 构建消息头
        MessageHeader header;
        header.body_length_ = static_cast<uint32_t>(body.size());
        header.message_id_  = message_id > 0 ? message_id : current_message_id_++;
        header.flags_       = flags;

        // 构建完整消息
        std::vector<char> buffer(sizeof(MessageHeader) + body.size());
        std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
        std::memcpy(buffer.data() + sizeof(MessageHeader), body.data(), body.size());

        boost::asio::async_write(socket_, boost::asio::buffer(buffer),
                                 [this, self](boost::system::error_code ec, std::size_t length) {
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
        boost::asio::async_read(socket_, boost::asio::buffer(&current_header_, sizeof(MessageHeader)),
                                [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec)
            {
                total_bytes_read_ += length;

                // 验证魔数
                if (current_header_.magic_ != 0x12345678)
                {
                    LOG_ERROR("Invalid message magic number");
                    doReadHeader();
                    return;
                }

                // 读取消息体
                if (current_header_.body_length_ > 0)
                {
                    body_buffer_.resize(current_header_.body_length_);
                    doReadBody();
                }
                else
                {
                    // 空消息体
                    processMessage("", current_header_);
                    doReadHeader();
                }
            }
            else
            {
                LOG_ERROR("Read header error: {}", ec.message());
            }
        });
    }

    void Session::doReadBody()
    {
        auto self(shared_from_this());
        boost::asio::async_read(socket_, boost::asio::buffer(body_buffer_),
                                [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec)
            {
                total_bytes_read_ += length;

                // 处理消息体
                std::string body(body_buffer_.begin(), body_buffer_.end());
                processMessage(body, current_header_);

                // 继续读取下一个消息
                doReadHeader();
            }
            else
            {
                LOG_ERROR("Read body error: {}", ec.message());
            }
        });
    }

    void Session::processMessage(std::string body, MessageHeader header)
    {
        // 解密
        if (header.flags_ & 0x02 && encryption_)
        {
            body = encryption_->Decrypt(body);
        }

        // 解压
        if (header.flags_ & 0x01 && compression_)
        {
            body = compression_->Decompress(body);
        }

        // 创建消息
        NetworkMessage message;

        // 检查是否是protobuf格式消息（flags第3位为1）
        if (header.flags_ & 0x04)
        {
            // 尝试通过注册的处理器创建消息
            auto proto_message = MessageHandlerRegistry::instance().createMessage(header.message_id_);

            if (proto_message)
            {
                // 反序列化消息
                if (!ProtobufUtil::Deserialize(body, *proto_message))
                {
                    LOG_ERROR("Failed to deserialize protobuf message, message_id={}", header.message_id_);
                    message = NetworkMessage::createTextMessage(header, body, socket_.remote_endpoint());
                }
                else
                {
                    LOG_DEBUG("Successfully deserialized protobuf message, message_id={}", header.message_id_);
                    message = NetworkMessage::createProtobufMessage(header, std::move(proto_message),
                                                                    socket_.remote_endpoint());
                }
            }
            else
            {
                LOG_WARN("No handler registered for message_id={}, keeping raw body", header.message_id_);
                message = NetworkMessage::createTextMessage(header, body, socket_.remote_endpoint());
            }
        }
        else
        {
            message = NetworkMessage::createTextMessage(header, body, socket_.remote_endpoint());
        }

        // 推送到Session自己的接收队列
        receive_queue_.push(message);
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
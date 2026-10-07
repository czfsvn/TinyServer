#include "network.h"

#include <algorithm>

#include "config.h"
#include "message_handler.h"

namespace cncpp
{
    namespace
    {
        // 一帧的编码产物：帧头必有，载荷可空。header 与 body 分开存放，
        // 配合 scatter-gather 一次写出，body 不再 memcpy 进临时大缓冲。
        // 整批产物挂在同一个 shared_ptr 上进 lambda，活到写完成回调为止。
        struct EncodedFrame
        {
            std::vector<char>                  header;       // kHeaderSize 字节定长帧头
            std::shared_ptr<const std::string> shared_body;  // 零拷贝：直接引用广播共享的载荷
            std::string                        owned_body;   // 压缩/加密的产物，无法共享

            // 本帧实际要写出的载荷：走了传输层变换就是自持的那份，否则是共享的那份
            const std::string& payload() const { return shared_body ? *shared_body : owned_body; }
        };
    }  // namespace

    // Session 类实现
    Session::Session(tcp::socket socket)
        : socket_(std::move(socket)),
          total_bytes_read_(0),
          total_bytes_written_(0),
          encryption_(nullptr),
          compression_(nullptr),
          // 容量只在构造时读一次：项目没有配置热更新通道（signal_handler.cpp 的
          // SIGHUP 还是个空 TODO）。两个数都兜底到至少 1 —— 配成 0 会让队列变成
          // "每条都丢"，那是配置错误，静默全丢比启动失败更难排查。
          send_queue_(std::max<size_t>(1, sMainConfig.send_queue_capacity())),
          is_sending_(false),
          receive_queue_(std::max<size_t>(1, sMainConfig.receive_queue_capacity()))
    {
        // socket 是被 async_accept(target_ctx, ...) / connect 建在目标 io_context 上的，
        // 把它的 executor 缓存下来：后续所有操作都要 post 回这个 executor。
        executor_ = socket_.get_executor();
    }

    Session::~Session()
    {
        // 析构里不能 post —— 无法保证 io_context 还会执行它。只能就地关。
        doClose();
    }

    void Session::start()
    {
        // 读链必须在本 Session 所属的 io_context 上启动。
        // accept 回调跑在 accept context（主线程）上，直接 doReadHeader() 会让这个
        // Session 的全部 handler 都跑到 accept context 的线程去，分发就白做了。
        auto self(shared_from_this());
        postToContext([this, self]() {
            doStart();
        });
    }

    void Session::doStart()
    {
        // Nagle 会把小包攒着等对端 ACK 才合并发出，状态同步类的小包会被拖出
        // 几百毫秒的延迟尖刺。协议帧自带长度前缀、批量发送由上层 scatter-gather
        // 负责，无需依赖 Nagle 合包，直接关掉。
        boost::system::error_code ec;
        socket_.set_option(tcp::no_delay(true), ec);
        if (ec)
        {
            LOG_WARN("Set TCP_NODELAY failed: {}", ec.message());
        }

        remote_endpoint_ = socket_.remote_endpoint(ec);
        if (ec)
        {
            LOG_WARN("Cannot resolve remote endpoint: {}", ec.message());
        }

        doReadHeader();
    }

    void Session::postToContext(std::function<void()> fn)
    {
        if (!executor_)
        {
            // 拿不到 executor 就就地执行，总好过把操作丢掉
            fn();
            return;
        }

        boost::asio::post(executor_, std::move(fn));
    }

    void Session::send(const std::string& body, uint32_t message_id)
    {
        send(body, message_id, PayloadFormat::kRaw);
    }

    void Session::send(const google::protobuf::Message& message, uint32_t message_id)
    {
        // 单播走这里仍然是"每个 Session 序列化一次"。广播别逐个调这个重载，
        // 而是 makeSharedPayload() 序列化一次、把同一份 shared_ptr 发给所有人。
        std::string serialized_message = ProtobufUtil::Serialize(message);
        if (!serialized_message.empty())
        {
            send(std::make_shared<const std::string>(std::move(serialized_message)), message_id,
                 PayloadFormat::kProtobuf);
        }
    }

    std::shared_ptr<const std::string> Session::makeSharedPayload(const google::protobuf::Message& message)
    {
        return std::make_shared<const std::string>(ProtobufUtil::Serialize(message));
    }

    std::shared_ptr<const std::string> Session::makeSharedPayload(std::string body)
    {
        return std::make_shared<const std::string>(std::move(body));
    }

    void Session::send(std::shared_ptr<const std::string> body, uint32_t message_id)
    {
        send(std::move(body), message_id, PayloadFormat::kRaw);
    }

    void Session::send(const std::string& body, uint32_t message_id, PayloadFormat format)
    {
        // 入队前必须有一份独立持有的字节块：调用方随时可能改/销毁 body
        send(std::make_shared<const std::string>(body), message_id, format);
    }

    void Session::send(std::shared_ptr<const std::string> body, uint32_t message_id, PayloadFormat format)
    {
        if (!body)
        {
            LOG_WARN("Session::send dropped null payload, message_id={}", message_id);
            return;
        }

        // 直接把共享块交出去，不复制：body 是 const 的，io 线程只读。
        // 到上限就弃载（ADR-0010）：内存恒定、连接存活，代价是这条下行消息丢了。
        if (!send_queue_.tryPush(std::make_tuple(std::move(body), message_id, format)))
        {
            if (++send_queue_full_drops_ % 1024 == 1)
            {
                LOG_WARN("Send queue full, dropped {} messages on this session", send_queue_full_drops_);
            }
            return;
        }

        // kick 握手的**生产端**一半（消费端一半在 processSendQueue 里，两边是一个
        // 整体，改动前先读那边的注释）：已经有写链在跑就只入队，它完成时会自己回来取。
        //
        // 用 CAS 而不是 load 判一次再 store：生产端可能有多个线程（tick 线程 + 任意
        // 业务线程），load 判过之后会被别人抢先，两个线程各 post 一次就多出一条空转
        // 写链。用默认内存序（seq_cst）—— 这里每批才走一次，不是热路径，不值得为了
        // 省几个周期去推理内存序。
        bool expected = false;
        if (is_sending_.compare_exchange_strong(expected, true))
        {
            // 写链的第一步（async_write）必须在本 Session 所属的 io_context 线程上发起。
            // tick 线程直接调 processSendQueue() 会在错误的线程上启动 async_write，
            // 与该 context 线程上正在跑的写完成 handler 并发。
            auto self(shared_from_this());
            postToContext([this, self]() {
                processSendQueue();
            });
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
        // 关连接必须和 pending 的 async_read / async_write 串行：
        // tick 线程直接 shutdown+close 时，io 线程可能正拿着这个 socket 在 handler 里。
        // 一律走 post，不做"当前线程是否就是 io 线程"的判断——
        // 延迟一拍关闭没有代价，判断错了代价很大。
        auto self(shared_from_this());
        postToContext([this, self]() {
            doClose();
        });
    }

    void Session::doClose()
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
        // 写失败路径关过一次之后，这里的重复关闭会得到 bad_descriptor：
        // 属正常幂等，不算错误，别刷 ERROR 日志
        if (ec && ec != boost::asio::error::bad_descriptor)
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
    MyConcurrentQueue<NetworkMessage>& Session::getReceiveQueue()
    {
        return receive_queue_;
    }

    void Session::processSendQueue()
    {
        // 编码-发送主循环：取空队列 → 逐帧压缩/加密/编码 → 一次 scatter-gather 写。
        // 外层 for(;;) 处理"整批全是超限帧被丢光"的情况：回头取新队列，
        // 不用递归（连续超限时递归会吃穿调用栈）。
        for (;;)
        {
            // 1. 取空发送队列。
            //
            // 取空与 is_sending_ 归零原本靠 send_queue_mutex_ 关在同一个临界区里，
            // 换成无锁队列后没有临界区了，改成**双检**。两端合起来覆盖所有交错顺序：
            //   消费端（这里）：清标志 → 复查非空则 CAS 回 true 并继续
            //   生产端（send）：入队   → 标志为 false 才 CAS 成 true 并 post
            // 少了任何一半，都会出现"消息入了队但再也没人发"的永久静默
            // （lost wakeup），而且只在特定时序下偶发，测试和线上都极难复现。
            //
            // 所有交错都能被覆盖：
            //   生产端在消费端清标志前入队  → 消费端复查看得到，自己接着发
            //   生产端在清标志后、复查前入队 → 消费端复查看得到，自己接着发
            //   生产端在复查之后才入队      → 它看到标志为 false，自己 post
            //   两端同时抢标志             → CAS 只有一个成功，成功那一方负责发
            std::vector<SendQueueItem> frames;
            frames.reserve(send_queue_.size());
            {
                SendQueueItem item;
                while (send_queue_.tryPop(item))
                {
                    frames.push_back(std::move(item));
                }
            }

            if (frames.empty())
            {
                // 消费端一半：先清标志，再复查。顺序不能反 —— 反过来的话，中间
                // 进来的 send() 看到标志仍为 true 就只入队不 kick，那条消息没人发了。
                is_sending_.store(false);

                if (!send_queue_.empty())
                {
                    // 复查到新消息，抢回标志继续发。抢不到说明生产端已经看到标志
                    // 为 false 并 CAS 走了，它会 post 一次，交给它。
                    bool expected = false;
                    if (!is_sending_.compare_exchange_strong(expected, true))
                    {
                        return;
                    }
                    continue;
                }
                return;
            }

            // 2. 逐帧压缩/加密/编码帧头；超限帧丢弃（沿用原单帧路径的策略）。
            //    编码产物挂在 shared_ptr 上：async_write 只持有内存引用，
            //    本函数发起写后即返回，产物必须活到写完成回调。
            auto encoded = std::make_shared<std::vector<EncodedFrame>>();
            encoded->reserve(frames.size());

            for (auto& frame : frames)
            {
                std::shared_ptr<const std::string> body = std::move(std::get<0>(frame));
                const auto                         msg_id = std::get<1>(frame);
                const auto                         format = std::get<2>(frame);

                if (!body)
                {
                    continue;
                }

                // 与读侧对称的传输层变换：发送侧先压缩、再加密。
                //
                // 变换产物只能由本帧自持（无法与别人共享），落到 owned_body；
                // 没配压缩/加密时直接引用共享载荷 —— 广播给 N 个 Session 时，
                // 这一帧的字节从头到尾只有一份，入队和编码都不再复制。
                uint16_t flags = 0;
                EncodedFrame out;
                if (compression_ || encryption_)
                {
                    std::string transformed = *body;
                    if (compression_)
                    {
                        transformed = compression_->Compress(transformed);
                        flags      = static_cast<uint16_t>(flags | kFlagCompressed);
                    }
                    if (encryption_)
                    {
                        transformed = encryption_->Encrypt(transformed);
                        flags      = static_cast<uint16_t>(flags | kFlagEncrypted);
                    }
                    out.owned_body = std::move(transformed);
                }
                else
                {
                    out.shared_body = std::move(body);
                }

                const std::string& payload = out.payload();
                if (payload.size() > kMaxBodyLength)
                {
                    LOG_ERROR("Dropping outgoing message_id={}: body {} exceeds limit {}", msg_id, payload.size(),
                              kMaxBodyLength);
                    continue;
                }

                // 帧头（format 是载荷语义，flags 只是传输层变换，两者正交）
                MessageHeader header;
                header.format_      = format;
                header.flags_       = flags;
                header.body_length_ = static_cast<uint32_t>(payload.size());
                header.message_id_  = msg_id;

                out.header.resize(kHeaderSize);
                encodeHeader(header, out.header.data());
                encoded->push_back(std::move(out));
            }

            // buffer 视图必须等编码全部结束后再建：视图指向 encoded 内部元素，
            // 此后 encoded 不再改动，地址才稳定
            std::vector<boost::asio::const_buffer> buffers;
            buffers.reserve(encoded->size() * 2);
            for (const auto& out : *encoded)
            {
                buffers.emplace_back(boost::asio::buffer(out.header));
                const std::string& payload = out.payload();
                if (!payload.empty())
                {
                    buffers.emplace_back(boost::asio::buffer(payload.data(), payload.size()));
                }
            }

            if (buffers.empty())
            {
                continue;  // 整批都被丢弃：回头取新消息
            }

            // 3. 一次提交整批：N 条消息的 N 次 write 系统调用合并为 1 次。
            //    写失败（对端断开/EPIPE 等）意味着字节流状态已不可信——
            //    async_write 可能只写出半帧，对端拿到的是残破帧序列，
            //    继续发下一条只会雪上加霜，必须关连接。
            auto self(shared_from_this());
            boost::asio::async_write(socket_, buffers,
                                     [this, self, encoded](boost::system::error_code ec, std::size_t length) {
                if (ec)
                {
                    LOG_ERROR("Send error after {} bytes, closing connection: {}", length, ec.message());
                    doClose();

                    // 清空队列并复位 is_sending_：连接已死。不复位的话，后续
                    // send() 看到 kick 标志仍为 true 就只入队不 kick，
                    // 消息会无限堆在死会话的队列里。
                    //
                    // 逐条 tryPop 而不是换一个新队列对象：深度计数也在队列内部，
                    // 走 tryPop 才会被正确减回去，直接丢对象虽然也能释放，但
                    // processSendQueue 的取空循环就可能读到脏状态。
                    SendQueueItem discarded;
                    while (send_queue_.tryPop(discarded))
                    {
                    }
                    is_sending_.store(false);
                    return;
                }

                total_bytes_written_ += length;

                // 写完回来看队列：写期间新到的消息已入队，这里再取下一批
                processSendQueue();
            });
            return;
        }
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

        // 投递到接收队列。到上限时丢弃，这是弃载而不是背压（见 CONTEXT.md）：
        // io 线程从不为队列减速，直接把新帧扔掉。tick 这一拍没跟上，让 io 线程
        // 等它只会把慢扩散到所有连接上。每 1024 条才报一次警，否则持续过载会
        // 先于真正的问题把 io 线程淹死在日志里。
        auto enqueue = [this](NetworkMessage&& message) {
            if (receive_queue_.tryPush(std::move(message)))
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

        // decodeHeader() 已经把非法 format 挡在前面（kBadFormat 会让连接直接关掉），
        // 所以这里到不了。但少一个返回值就会掉出非 void 函数末尾 —— 返回的是垃圾，
        // 而调用方拿它当「连接是否保持」，等于随机关连接（GCC: control reaches end
        // of non-void function）。兜底显式关连接，别把 UB 留在协议解析的热路径上。
        LOG_ERROR("Unknown payload format {}, closing connection", static_cast<int>(header.format_));
        return false;
    }

    // Acceptor 类实现
    Acceptor::Acceptor(boost::asio::io_context& io_context, short port, ConnectionCallback connection_callback,
                       ContextPicker session_ctx_picker)
        : acceptor_(io_context),
          connection_callback_(connection_callback),
          session_ctx_picker_(std::move(session_ctx_picker))
    {
        // 显式 open -> SO_REUSEADDR -> bind -> listen 四步，替代 (io_context, endpoint) 构造：
        // 两参构造虽然默认也置 reuse_addr，但整条链路藏在 asio 里不可见，出错只有笼统异常。
        // 分步做：意图显式，且每一步报错都带得上端口号。
        // SO_REUSEADDR：Linux 上服务重启不被 TIME_WAIT 状态的旧连接卡住端口。
        boost::system::error_code ec;
        acceptor_.open(tcp::v4(), ec);
        if (ec)
        {
            throw boost::system::system_error(ec, "acceptor open");
        }

        acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
        if (ec)
        {
            LOG_WARN("Set SO_REUSEADDR failed: {}", ec.message());
        }

        acceptor_.bind(tcp::endpoint(tcp::v4(), port), ec);
        if (ec)
        {
            throw boost::system::system_error(ec, "acceptor bind port " + std::to_string(port));
        }

        acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
        if (ec)
        {
            throw boost::system::system_error(ec, "acceptor listen port " + std::to_string(port));
        }
    }

    void Acceptor::start()
    {
        // stop() 会 close 掉 acceptor_：对已关闭的句柄发起 async_accept 会立刻得到
        // bad_descriptor，而 doAccept 见 is_running_ 为真又会重新挂——
        // 变成"每次立即报错-再挂"的死循环。这里直接拒绝启动。
        if (!acceptor_.is_open())
        {
            LOG_ERROR("Acceptor cannot start: socket is closed");
            return;
        }

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

        // 关键：把新连接直接接收到 picker 挑出的 io_context 上。
        // 不带 context 参数的重载会用 acceptor 自己的 io_context 建 socket，
        // 结果就是所有连接全堆在同一个 context 上 —— 改造前正是这样。
        if (session_ctx_picker_)
        {
            acceptor_.async_accept(session_ctx_picker_(), async_accept_handler);
        }
        else
        {
            acceptor_.async_accept(async_accept_handler);
        }
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
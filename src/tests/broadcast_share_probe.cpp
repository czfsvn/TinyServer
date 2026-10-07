// broadcast_share_probe.cpp
//
// 目的：验证"广播共享"这条优化真的生效 —— 同一个序列化结果被 N 个 Session 共享，
//       而不是每个接收者一份拷贝。
//
// 手法上的关键点：**不启动 worker 线程**，所有 io_context 都由主线程 poll() 驱动。
// 这样"广播完立刻读 use_count()"是确定性的 —— 如果有消费者线程在并发取队列，
// 这个数会随调度时机乱跳，测出来的东西没有意义。
//
// 检查两件事：
//   1. 共享：广播给 N 个 Session 之后 payload.use_count() 恰好 +N
//   2. 正确：N 个客户端都收到字节完全一致、且内容正确的帧

#include <boost/asio.hpp>
#include <array>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "network.h"

using boost::asio::ip::tcp;

namespace
{
    constexpr short    kPort      = 9091;
    constexpr size_t   kConns     = 32;
    constexpr size_t   kWorkers   = 4;
    constexpr uint32_t kMsgId     = 4242;
    constexpr size_t   kBodyBytes = 512;
}  // namespace

int main()
{
    boost::asio::io_context accept_ctx;

    std::vector<std::shared_ptr<boost::asio::io_context>> workers;
    for (size_t i = 0; i < kWorkers; ++i)
    {
        workers.push_back(std::make_shared<boost::asio::io_context>());
    }

    // 声明顺序 = 析构逆序：acceptor 先死（停止收连接），sessions 在 worker
    // 之前死（Session 析构要关 socket，那时它的 context 还活着）
    std::vector<std::shared_ptr<cncpp::Session>> sessions;
    size_t                                      next_worker = 0;

    auto acceptor = std::make_shared<cncpp::Acceptor>(
        accept_ctx, kPort,
        [&sessions](tcp::socket&& sock) {
            auto session = std::make_shared<cncpp::Session>(std::move(sock));
            sessions.push_back(session);
            session->start();
        },
        [&workers, &next_worker]() -> boost::asio::io_context& {
            boost::asio::io_context& ctx = *workers[next_worker % workers.size()];
            ++next_worker;
            return ctx;
        });
    acceptor->start();

    std::vector<std::unique_ptr<tcp::socket>> clients;
    for (size_t i = 0; i < kConns; ++i)
    {
        auto                      sock = std::make_unique<tcp::socket>(accept_ctx);
        boost::system::error_code ec;
        sock->connect(tcp::endpoint(boost::asio::ip::address_v4::loopback(), kPort), ec);
        if (ec)
        {
            std::cout << "connect #" << i << " failed: " << ec.message() << std::endl;
            return 1;
        }
        clients.push_back(std::move(sock));
    }

    // 驱动 accept + Session::start()（后者 post 到各自的 worker context）
    for (int round = 0; round < 5; ++round)
    {
        while (accept_ctx.poll())
        {
        }
        for (auto& worker : workers)
        {
            while (worker->poll())
            {
            }
        }
    }

    std::cout << "conns=" << kConns << " workers=" << kWorkers << " port=" << kPort << std::endl;
    std::cout << "------------------------------------------" << std::endl;

    bool all_ok = true;

    if (sessions.size() != kConns)
    {
        std::cout << "FAIL: expected " << kConns << " sessions, got " << sessions.size() << std::endl;
        return 1;
    }

    // 1. 广播：序列化一次，同一份 shared_ptr 交给所有接收者
    const std::string expected(kBodyBytes, 'Z');
    auto              payload = cncpp::Session::makeSharedPayload(std::string(expected));

    const long use_before = payload.use_count();
    for (auto& session : sessions)
    {
        session->send(payload, kMsgId);
    }
    const long use_after = payload.use_count();

    // 共享成立的判据：每个 Session 的发送队列各持一份引用，本地再持一份。
    // 若退化成"每人一份拷贝"，use_count 会停在 1。
    const long expect_after = use_before + static_cast<long>(kConns);
    const bool shared       = (use_after == expect_after);
    all_ok                  = all_ok && shared;

    std::cout << "use_count before = " << use_before << std::endl;
    std::cout << "use_count after  = " << use_after << " (expect " << expect_after << ")" << std::endl;
    std::cout << "sharing          = " << (shared ? "YES" : "NO") << std::endl;

    // 2. 驱动写链：send() 只 post 了 kick，真正的编码和 write 发生在 worker context 上
    for (int round = 0; round < 10; ++round)
    {
        for (auto& worker : workers)
        {
            while (worker->poll())
            {
            }
        }
    }

    // 3. 每个客户端读一帧，校验 message_id 与 body
    size_t verified = 0;
    for (size_t i = 0; i < clients.size(); ++i)
    {
        std::array<unsigned char, cncpp::kHeaderSize> header_buf{};
        boost::system::error_code                     ec;
        boost::asio::read(*clients[i], boost::asio::buffer(header_buf), ec);
        if (ec)
        {
            std::cout << "client #" << i << " read header failed: " << ec.message() << std::endl;
            all_ok = false;
            continue;
        }

        cncpp::MessageHeader header;
        if (cncpp::decodeHeader(header_buf.data(), header) != cncpp::HeaderError::kOk)
        {
            std::cout << "client #" << i << " bad frame header" << std::endl;
            all_ok = false;
            continue;
        }

        std::string body(header.body_length_, '\0');
        if (header.body_length_ > 0)
        {
            boost::asio::read(*clients[i], boost::asio::buffer(body.data(), body.size()), ec);
            if (ec)
            {
                std::cout << "client #" << i << " read body failed: " << ec.message() << std::endl;
                all_ok = false;
                continue;
            }
        }

        if (header.message_id_ != kMsgId || body != expected)
        {
            std::cout << "client #" << i << " mismatch: id=" << header.message_id_ << " body_size=" << body.size()
                      << std::endl;
            all_ok = false;
            continue;
        }
        ++verified;
    }

    std::cout << "verified         = " << verified << " / " << kConns << std::endl;
    std::cout << "body bytes       = " << kBodyBytes << " x " << kConns << " recipients" << std::endl;

    acceptor->stop();
    while (accept_ctx.poll())
    {
    }

    std::cout << "------------------------------------------" << std::endl;
    std::cout << (all_ok ? "PASS" : "FAIL") << ": "
              << (all_ok ? "广播只序列化/分配一次，N 个 Session 共享同一份载荷，且全部收到正确帧"
                         : "广播共享未成立或帧内容有误")
              << std::endl;
    return all_ok ? 0 : 1;
}

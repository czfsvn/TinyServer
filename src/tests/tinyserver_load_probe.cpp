// 真实流量压测客户端：对 tinyserver 建 N 条连接，每条发 R 帧 kRaw 消息并逐帧校验回显。
//
// 目的不是测吞吐，而是验证接进真实代码之后的两条链路：
//   1. 入站：worker io_context 上的 Session 读帧 -> SPSC 队列 -> tick 线程抽干
//   2. 出站：tick 线程 -> TcpTask::sendMessage() -> Session::send() -> post 回 worker context
//
// 第 2 条就是 N4：修之前 async_write 会在主线程上发起，与 worker 线程上正在跑的
// 写完成 handler 并发，前提就破了。回显是唯一能压到这条路径的最小业务。
//
// 用法：tinyserver_load_probe [conns] [rounds] [port]

#include "message.h"

#include <boost/asio.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using boost::asio::ip::tcp;

namespace
{
    struct ConnStats
    {
        uint64_t sent     = 0;
        uint64_t received = 0;
        uint64_t mismatch = 0;
        uint64_t error    = 0;
    };

    void runConnection(unsigned idx, unsigned rounds, unsigned short port, ConnStats& out)
    {
        try
        {
            boost::asio::io_context io;
            tcp::socket             sock(io);
            sock.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port));

            std::vector<char> hdr(cncpp::kHeaderSize);

            for (unsigned r = 0; r < rounds; ++r)
            {
                const std::string body = "PING:" + std::to_string(idx) + ":" + std::to_string(r);

                cncpp::MessageHeader h;
                h.format_      = cncpp::PayloadFormat::kRaw;
                h.body_length_ = static_cast<uint32_t>(body.size());
                h.message_id_  = 1;
                cncpp::encodeHeader(h, hdr.data());

                boost::system::error_code ec;
                boost::asio::write(sock, boost::asio::buffer(hdr), ec);
                if (ec)
                {
                    ++out.error;
                    return;
                }
                boost::asio::write(sock, boost::asio::buffer(body), ec);
                if (ec)
                {
                    ++out.error;
                    return;
                }
                ++out.sent;

                // 读回显帧
                boost::asio::read(sock, boost::asio::buffer(hdr), ec);
                if (ec)
                {
                    ++out.error;
                    return;
                }

                cncpp::MessageHeader rh;
                if (cncpp::decodeHeader(hdr.data(), rh) != cncpp::HeaderError::kOk)
                {
                    ++out.error;
                    return;
                }

                std::string echoed(rh.body_length_, '\0');
                if (rh.body_length_ > 0)
                {
                    boost::asio::read(sock, boost::asio::buffer(echoed), ec);
                    if (ec)
                    {
                        ++out.error;
                        return;
                    }
                }

                ++out.received;
                if (echoed != body)
                {
                    ++out.mismatch;
                }
            }
        }
        catch (const std::exception&)
        {
            ++out.error;
        }
    }

}  // namespace

int main(int argc, char* argv[])
{
    const unsigned       conns  = (argc > 1) ? static_cast<unsigned>(std::atoi(argv[1])) : 32;
    const unsigned       rounds = (argc > 2) ? static_cast<unsigned>(std::atoi(argv[2])) : 100;
    const unsigned short port   = (argc > 3) ? static_cast<unsigned short>(std::atoi(argv[3])) : 9090;

    std::cout << "conns=" << conns << " rounds=" << rounds << " port=" << port << std::endl;

    std::vector<ConnStats>   stats(conns);
    std::vector<std::thread> threads;

    const auto t0 = std::chrono::steady_clock::now();

    for (unsigned i = 0; i < conns; ++i)
    {
        threads.emplace_back(runConnection, i, rounds, port, std::ref(stats[i]));
    }

    for (auto& t : threads)
    {
        t.join();
    }

    const auto elapsed_ms
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    uint64_t sent = 0, received = 0, mismatch = 0, error = 0;
    for (const auto& s : stats)
    {
        sent += s.sent;
        received += s.received;
        mismatch += s.mismatch;
        error += s.error;
    }

    std::cout << "------------------------------------------" << std::endl;
    std::cout << "sent      = " << sent << std::endl;
    std::cout << "received  = " << received << std::endl;
    std::cout << "mismatch  = " << mismatch << std::endl;
    std::cout << "error     = " << error << std::endl;
    std::cout << "elapsed   = " << elapsed_ms << " ms" << std::endl;

    const uint64_t expected = static_cast<uint64_t>(conns) * rounds;
    const bool     ok       = (sent == expected) && (received == expected) && (mismatch == 0) && (error == 0);

    std::cout << (ok ? "PASS" : "FAIL") << ": expected " << expected << " echoes" << std::endl;
    return ok ? 0 : 1;
}

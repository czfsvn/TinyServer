// io_context_accept_probe.cpp
//
// 目的：验证 Asio 的 acceptor.async_accept(context, token) 重载能否把新连接
//       直接接收到「另一个」io_context 上，且该连接的后续读写 handler 确实
//       运行在目标 io_context 的线程上（Windows / IOCP 下同样成立）。
//
// 这是 TinyServer IOContextPool 改造（多 io_context、每个 io_context 承载多个
// Session）的前置可行性验证。
//
// 编译（clang++ 18 + MSVC target，Boost 头文件在 $BOOST_ROOT）：
//   clang++ -std=c++17 -O1 -DBOOST_ASIO_HAS_STD_INVOKE_RESULT -DBOOST_ASIO_HAS_STD_STRING_VIEW
//           -I "%BOOST_ROOT%" -o accept_probe.exe io_context_accept_probe.cpp -lws2_32 -lmswsock
//
// 用法：
//   accept_probe.exe [worker_count] [conn_count]

#include <boost/asio.hpp>
#include <boost/version.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using boost::asio::ip::tcp;
using namespace std::chrono_literals;

namespace
{

constexpr int kPort = 18081;

struct ConnResult
{
    int          index        = -1;
    int          expect_ctx   = -1;
    bool         owned_by_ctx = false;  // socket 的 executor 是否属于目标 io_context
    bool         read_ok      = false;  // 读到客户端发的 "PING"
    bool         echo_ok      = false;  // 客户端收到 "PONG"
    bool         read_on_ctx  = false;  // async_read handler 跑在目标 ctx 线程
    bool         write_on_ctx = false;  // async_write handler 跑在目标 ctx 线程
    std::thread::id read_tid;
    std::thread::id write_tid;
};

std::mutex               g_results_mutex;
std::vector<ConnResult>  g_results;

#if BOOST_VERSION >= 107000
using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
#else
using WorkGuard = boost::asio::io_context::work;
#endif

std::unique_ptr<WorkGuard> makeWorkGuard(boost::asio::io_context& ctx)
{
#if BOOST_VERSION >= 107000
    return std::unique_ptr<WorkGuard>(new WorkGuard(boost::asio::make_work_guard(ctx)));
#else
    return std::unique_ptr<WorkGuard>(new WorkGuard(ctx));
#endif
}

}  // namespace

class Probe
{
public:
    Probe(int worker_count, int conn_count)
        : worker_count_(worker_count), conn_count_(conn_count), worker_tids_(worker_count)
    {
        g_results.resize(conn_count);
        for (int i = 0; i < conn_count; ++i)
        {
            g_results[i].index      = i;
            g_results[i].expect_ctx = i % worker_count;
        }
    }

    bool run()
    {
        // 1) N 个 io_context，每个一个线程（正是目标模型）
        for (int i = 0; i < worker_count_; ++i)
        {
            worker_ctxs_.push_back(std::make_unique<boost::asio::io_context>());
        }
        for (int i = 0; i < worker_count_; ++i)
        {
            worker_guards_.push_back(makeWorkGuard(*worker_ctxs_[i]));
        }

        std::mutex              tid_mutex;
        std::condition_variable tid_cv;
        int                     tid_ready = 0;

        for (int i = 0; i < worker_count_; ++i)
        {
            auto* ctx = worker_ctxs_[i].get();
            worker_threads_.emplace_back([this, i, ctx, &tid_mutex, &tid_cv, &tid_ready]() {
                {
                    std::lock_guard<std::mutex> lk(tid_mutex);
                    worker_tids_[i] = std::this_thread::get_id();
                    ++tid_ready;
                    tid_cv.notify_all();
                }
                ctx->run();
            });
        }

        {
            std::unique_lock<std::mutex> lk(tid_mutex);
            tid_cv.wait(lk, [&]() { return tid_ready == worker_count_; });
        }

        // 2) acceptor 放在独立的 accept io_context（主线程跑）
        boost::asio::io_context accept_ctx;
        tcp::acceptor           acceptor(accept_ctx, tcp::endpoint(tcp::v4(), kPort));

        std::atomic<int> accepted{0};

        std::function<void()> do_accept = [&]() {
            // 轮询挑目标 io_context —— 改造成"最少连接"时把这里换成选择器即可
            const int target = next_target_.fetch_add(1) % worker_count_;
            auto&     target_ctx = *worker_ctxs_[target];

            acceptor.async_accept(target_ctx,
                [this, &do_accept, &accepted, target](boost::system::error_code ec, tcp::socket sock) {
                    if (!ec)
                    {
                        const int idx = accepted.fetch_add(1);
                        onAccepted(idx, target, std::move(sock));
                    }
                    else
                    {
                        std::cerr << "[probe] accept error: " << ec.message() << "\n";
                    }

                    if (accepted.load() < conn_count_)
                    {
                        do_accept();
                    }
                });
        };

        do_accept();

        // 3) 客户端线程：阻塞建连并做一次 echo 往返
        std::thread client([this, &accept_ctx]() { clientLoop(accept_ctx); });

        accept_ctx.run();   // 主线程跑 accept

        client.join();

        // 4) 等服务端写 handler 都跑完（客户端收到 PONG 不代表 handler 已记账）
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (writes_done_.load() < conn_count_ && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(10ms);
        }

        // 5) 收尾
        for (auto& g : worker_guards_)
        {
            g.reset();
        }
        for (auto& c : worker_ctxs_)
        {
            c->stop();
        }
        for (auto& t : worker_threads_)
        {
            t.join();
        }

        return report();
    }

private:
    void onAccepted(int idx, int target, tcp::socket sock)
    {
        auto& result = g_results[idx];

        // (a) socket 的 executor 是否真的属于目标 io_context
        result.owned_by_ctx =
            (std::addressof(sock.get_executor().context()) == worker_ctxs_[target].get());

        // (b) Session 必须在自己的 io_context 上启动读链
        auto session     = std::make_shared<Session>(this, std::move(sock), idx, target);
        auto& target_ctx = *worker_ctxs_[target];
        boost::asio::post(target_ctx, [session]() { session->start(); });
    }

    struct Session : std::enable_shared_from_this<Session>
    {
        Session(Probe* owner, tcp::socket sock, int idx, int target)
            : owner_(owner), sock_(std::move(sock)), idx_(idx), target_(target)
        {
        }

        void start()
        {
            auto self = shared_from_this();
            boost::asio::async_read(sock_, boost::asio::buffer(buf_, 4),
                [this, self](boost::system::error_code ec, std::size_t n) {
                    std::lock_guard<std::mutex> lk(g_results_mutex);
                    auto&                        r = g_results[idx_];
                    r.read_tid                     = std::this_thread::get_id();
                    r.read_on_ctx = (r.read_tid == owner_->worker_tids_[target_]);
                    r.read_ok = (!ec && n == 4 && std::memcmp(buf_, "PING", 4) == 0);
                    if (!r.read_ok)
                    {
                        return;
                    }
                    doWrite();
                });
        }

        void doWrite()
        {
            auto self = shared_from_this();
            boost::asio::async_write(sock_, boost::asio::buffer("PONG", 4),
                [this, self](boost::system::error_code ec, std::size_t n) {
                    {
                        std::lock_guard<std::mutex> lk(g_results_mutex);
                        auto&                        r = g_results[idx_];
                        r.write_tid                    = std::this_thread::get_id();
                        r.write_on_ctx = (r.write_tid == owner_->worker_tids_[target_]);
                        r.echo_ok      = (!ec && n == 4);
                    }
                    owner_->writes_done_.fetch_add(1);
                    boost::system::error_code ignored;
                    sock_.shutdown(tcp::socket::shutdown_both, ignored);
                    sock_.close(ignored);
                });
        }

        Probe*      owner_;
        tcp::socket sock_;
        char        buf_[4]{};
        int         idx_;
        int         target_;
    };

    void clientLoop(boost::asio::io_context& accept_ctx)
    {
        // client_ctx 不 run()，因此可以用阻塞式 connect/read/write
        boost::asio::io_context client_ctx;

        for (int i = 0; i < conn_count_; ++i)
        {
            tcp::socket sock(client_ctx);
            boost::system::error_code ec;
            sock.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), kPort), ec);
            if (ec)
            {
                std::cerr << "[probe] client connect failed: " << ec.message() << "\n";
                continue;
            }

            boost::asio::write(sock, boost::asio::buffer("PING", 4), ec);
            if (ec)
            {
                std::cerr << "[probe] client write failed: " << ec.message() << "\n";
                continue;
            }

            char resp[4] = {0};
            boost::asio::read(sock, boost::asio::buffer(resp, 4), ec);
            if (ec)
            {
                std::cerr << "[probe] client read failed: " << ec.message() << "\n";
                continue;
            }
            if (std::memcmp(resp, "PONG", 4) != 0)
            {
                std::cerr << "[probe] client got bad echo on conn " << i << "\n";
            }

            sock.close(ec);
        }

        accept_ctx.stop();
    }

    bool report()
    {
        std::cout << "\nBoost version: " << BOOST_VERSION << "\n";
        std::cout << "workers=" << worker_count_ << " conns=" << conn_count_ << "\n\n";
        std::cout << std::left << std::setw(6) << "conn" << std::setw(8) << "ctx" << std::setw(12)
                  << "owned" << std::setw(8) << "read" << std::setw(8) << "echo" << std::setw(12)
                  << "read_ctx" << std::setw(12) << "write_ctx" << "\n";
        std::cout << std::string(66, '-') << "\n";

        bool all_ok = true;
        for (const auto& r : g_results)
        {
            const bool ok = r.owned_by_ctx && r.read_ok && r.echo_ok && r.read_on_ctx
                            && r.write_on_ctx;
            all_ok = all_ok && ok;

            std::cout << std::left << std::setw(6) << r.index << std::setw(8) << r.expect_ctx
                      << std::setw(12) << (r.owned_by_ctx ? "YES" : "NO") << std::setw(8)
                      << (r.read_ok ? "ok" : "FAIL") << std::setw(8)
                      << (r.echo_ok ? "ok" : "FAIL") << std::setw(12)
                      << (r.read_on_ctx ? "YES" : "NO") << std::setw(12)
                      << (r.write_on_ctx ? "YES" : "NO") << "\n";
        }

        std::cout << "\n" << (all_ok ? "PASS" : "FAIL")
                  << ": async_accept(context, token) "
                  << (all_ok ? "把新连接投递到了目标 io_context，IO 也在该 context 线程上完成"
                             : "未能按预期工作，需要改用 release() + assign() 迁移方案")
                  << "\n";
        return all_ok;
    }

    int worker_count_;
    int conn_count_;

    std::vector<std::unique_ptr<boost::asio::io_context>> worker_ctxs_;
    std::vector<std::unique_ptr<WorkGuard>>               worker_guards_;
    std::vector<std::thread>                              worker_threads_;
    std::vector<std::thread::id>                          worker_tids_;

    std::atomic<int>   next_target_{0};
    std::atomic<int>   writes_done_{0};
};

int main(int argc, char** argv)
{
    const int worker_count = (argc > 1) ? std::atoi(argv[1]) : 4;
    const int conn_count   = (argc > 2) ? std::atoi(argv[2]) : 8;

    Probe probe(worker_count, conn_count);
    return probe.run() ? 0 : 1;
}

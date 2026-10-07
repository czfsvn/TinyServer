// io_context_session_probe.cpp
//
// 目的：验证「N 个 io_context，每个 io_context 一个线程，每个 context 上同时挂多个
//       Session」这个模型真的成立，并且单线程 context 的串行保证没有被破坏。
//
// 与 io_context_accept_probe 的区别（后者测不到的正是这个探针要测的）：
//   accept_probe 的 clientLoop 是串行建连的（connect→write→read→close 一条接一条），
//   同一时刻只有 1 个 Session 存活，只验证了「新连接能否投递到目标 io_context」。
//   本探针让 worker_count × sessions_per_ctx 条连接【同时存活】，每条跑 rounds 轮
//   echo；服务端 Session 上 async_read / async_write / steady_timer 三类异步操作
//   【同时挂着】，再用 HandlerGuard 逐 handler 检测同一 context 内是否出现重叠执行。
//
// 判定标准（全部满足才 PASS）：
//   1. 每个 socket 的 executor 属于目标 io_context（owned）
//   2. 同一 context 内 handler 从不重叠：max_concurrent == 1 且 overlap == 0
//   3. 所有 handler 都跑在各自 context 的线程上：affinity == 0
//   4. 所有客户端都收到 rounds 个 PONG（无数据错乱、无串包）
//
// 用法：
//   io_context_session_probe [worker_count] [sessions_per_ctx] [rounds]
//   默认 4 20 10 → 80 条连接同时存活，共 800 轮 echo

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
#include <utility>
#include <vector>

using boost::asio::ip::tcp;
using namespace std::chrono_literals;

namespace
{

constexpr int kPort = 18082;

struct CtxStats
{
    std::atomic<int>       sessions{0};
    std::atomic<long long> handler_calls{0};
    std::atomic<int>       active{0};              // 当前正在 handler 里的数量
    std::atomic<int>       max_active{0};          // 观测到的最大并发 handler 数
    std::atomic<int>       overlap_violations{0};  // 进入 handler 时已有 handler 在跑
    std::atomic<int>       affinity_violations{0};  // handler 跑错了线程
    std::thread::id        tid{};
};

struct SessionResult
{
    std::atomic<bool> owned{false};
    std::atomic<bool> connected{false};
    std::atomic<int>  pongs{0};
    std::atomic<int>  pushes{0};
    std::atomic<bool> completed{false};
};

// 每个 handler 开头放一个 HandlerGuard：登记「我正在跑」，析构时注销。
// 单线程 io_context 下 active 任何时刻都应该是 1；出现 2 就说明串行保证破了。
class HandlerGuard
{
public:
    explicit HandlerGuard(CtxStats& stats) : stats_(stats)
    {
        const int prev = stats_.active.fetch_add(1);
        const int cur  = prev + 1;
        if (prev != 0)
        {
            stats_.overlap_violations.fetch_add(1);
        }
        int m = stats_.max_active.load();
        while (cur > m && !stats_.max_active.compare_exchange_weak(m, cur))
        {
        }
        stats_.handler_calls.fetch_add(1);
        if (std::this_thread::get_id() != stats_.tid)
        {
            stats_.affinity_violations.fetch_add(1);
        }
    }

    ~HandlerGuard() { stats_.active.fetch_sub(1); }

    HandlerGuard(const HandlerGuard&)            = delete;
    HandlerGuard& operator=(const HandlerGuard&) = delete;

private:
    CtxStats& stats_;
};

auto makeWorkGuard(boost::asio::io_context& ctx)
{
    using Guard = decltype(boost::asio::make_work_guard(ctx));
    return std::unique_ptr<Guard>(new Guard(boost::asio::make_work_guard(ctx)));
}

}  // namespace

// 服务端 Session：常驻 async_read，收到 PING 后 async_write(PONG)，
// 同时挂一个 steady_timer 主动推一条 PUSH —— 这样同一 socket 上
// read / write / timer 三类异步操作是并存 pending 的。
class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(boost::asio::io_context& ctx, tcp::socket sock, CtxStats& stats, SessionResult& result)
        : ctx_(ctx), sock_(std::move(sock)), stats_(stats), result_(result), timer_(ctx)
    {
    }

    void start()
    {
        result_.owned = (std::addressof(sock_.get_executor().context()) == std::addressof(ctx_));
        stats_.sessions.fetch_add(1);

        doRead();
        doPush();
    }

private:
    void doRead()
    {
        auto self = shared_from_this();
        boost::asio::async_read(sock_, boost::asio::buffer(rbuf_, 4),
            [this, self](boost::system::error_code ec, std::size_t n) {
                HandlerGuard guard(stats_);
                if (ec || n != 4)
                {
                    return;  // 客户端断开，读链自然结束
                }
                if (std::memcmp(rbuf_, "PING", 4) == 0)
                {
                    doWrite("PONG");
                }
                doRead();  // 读链常驻，与写操作并存
            });
    }

    void doWrite(const char* text)
    {
        auto self = shared_from_this();
        auto buf  = std::make_shared<std::string>(text, 4);
        boost::asio::async_write(sock_, boost::asio::buffer(*buf),
            [this, self, buf](boost::system::error_code, std::size_t) {
                HandlerGuard guard(stats_);
            });
    }

    void doPush()
    {
        auto self = shared_from_this();
        timer_.expires_after(1ms);
        timer_.async_wait([this, self](boost::system::error_code ec) {
            HandlerGuard guard(stats_);
            if (ec)
            {
                return;
            }
            doWrite("PUSH");  // 与 doRead 的 pending read 并发挂在这条连接上
        });
    }

    boost::asio::io_context& ctx_;
    tcp::socket              sock_;
    CtxStats&                stats_;
    SessionResult&           result_;
    boost::asio::steady_timer timer_;
    char                     rbuf_[4]{};
};

// 客户端：一条连接跑 rounds 轮 echo；读循环常驻，能同时接 PONG 和服务端推的 PUSH
class Client : public std::enable_shared_from_this<Client>
{
public:
    Client(boost::asio::io_context& ctx, int rounds, SessionResult& result, std::atomic<int>& done)
        : sock_(ctx), rounds_(rounds), result_(result), done_(done)
    {
    }

    void start(const tcp::endpoint& ep)
    {
        auto self = shared_from_this();
        sock_.async_connect(ep, [this, self](boost::system::error_code ec) {
            if (ec)
            {
                return;
            }
            result_.connected = true;
            doRead();   // 先挂读，这样服务端推的 PUSH 也能接住
            doWrite();  // 再发第一个 PING
        });
    }

private:
    void doRead()
    {
        auto self = shared_from_this();
        boost::asio::async_read(sock_, boost::asio::buffer(rbuf_, 4),
            [this, self](boost::system::error_code ec, std::size_t n) {
                if (ec || n != 4)
                {
                    return;
                }
                if (std::memcmp(rbuf_, "PONG", 4) == 0)
                {
                    result_.pongs.fetch_add(1);
                    if (result_.pongs.load() < rounds_)
                    {
                        doWrite();
                    }
                    else
                    {
                        result_.completed = true;
                        done_.fetch_add(1);
                        boost::system::error_code ignored;
                        sock_.shutdown(tcp::socket::shutdown_both, ignored);
                        sock_.close(ignored);
                        return;
                    }
                }
                else if (std::memcmp(rbuf_, "PUSH", 4) == 0)
                {
                    result_.pushes.fetch_add(1);
                }
                else
                {
                    std::cerr << "[probe] 收到非预期报文，协议错乱\n";
                    return;
                }
                doRead();
            });
    }

    void doWrite()
    {
        auto self = shared_from_this();
        auto buf  = std::make_shared<std::string>("PING", 4);
        boost::asio::async_write(sock_, boost::asio::buffer(*buf),
            [this, self, buf](boost::system::error_code, std::size_t) {});
    }

    tcp::socket       sock_;
    int               rounds_;
    SessionResult&    result_;
    std::atomic<int>& done_;
    char              rbuf_[4]{};
};

class Probe
{
public:
    Probe(int worker_count, int sessions_per_ctx, int rounds)
        : worker_count_(worker_count), sessions_per_ctx_(sessions_per_ctx), rounds_(rounds),
          total_(worker_count * sessions_per_ctx), stats_(worker_count), results_(total_)
    {
        for (int i = 0; i < total_; ++i)
        {
            results_[i] = std::make_unique<SessionResult>();
        }
    }

    bool run()
    {
        // 1) N 个 io_context，每个一个线程
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
                    stats_[i].tid = std::this_thread::get_id();
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

        // 2) acceptor 放在独立 accept_ctx（主线程跑），收满即止
        boost::asio::io_context accept_ctx;
        tcp::acceptor           acceptor(accept_ctx, tcp::endpoint(tcp::v4(), kPort));
        std::atomic<int>        accepted{0};

        std::function<void()> do_accept = [&]() {
            const int target = accepted.load() % worker_count_;
            acceptor.async_accept(*worker_ctxs_[target],
                [this, &do_accept, &accepted, target](
                    boost::system::error_code ec, tcp::socket sock) {
                    if (!ec)
                    {
                        const int idx = accepted.fetch_add(1);
                        auto     session = std::make_shared<Session>(
                            *worker_ctxs_[target], std::move(sock), stats_[target], *results_[idx]);
                        boost::asio::post(
                            *worker_ctxs_[target], [session]() { session->start(); });
                    }
                    else
                    {
                        std::cerr << "[probe] accept error: " << ec.message() << "\n";
                    }

                    if (accepted.load() < total_)
                    {
                        do_accept();  // 收满后不再续 accept，accept_ctx 自然没活干，run() 返回
                    }
                });
        };

        do_accept();

        // 3) 客户端：一个 client_ctx + 一个线程，全部连接同时建起并保持存活
        boost::asio::io_context client_ctx;
        auto                    client_guard = makeWorkGuard(client_ctx);
        std::thread             client_thread([&]() { client_ctx.run(); });

        const tcp::endpoint ep(boost::asio::ip::make_address("127.0.0.1"), kPort);
        for (int i = 0; i < total_; ++i)
        {
            auto c = std::make_shared<Client>(client_ctx, rounds_, *results_[i], done_count_);
            clients_.push_back(c);
            c->start(ep);
        }

        accept_ctx.run();  // 主线程 accept，收满 total_ 条后返回

        // 4) 等所有客户端跑完 rounds 轮
        const auto deadline = std::chrono::steady_clock::now() + 60s;
        while (done_count_.load() < total_ && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(10ms);
        }

        // 5) 收尾
        client_guard.reset();
        client_ctx.stop();
        client_thread.join();

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
    bool report() const
    {
        std::cout << "\nBoost version: " << BOOST_VERSION << "\n";
        std::cout << "workers=" << worker_count_ << " sessions_per_ctx=" << sessions_per_ctx_
                  << " rounds=" << rounds_ << "  (total sessions=" << total_ << ")\n\n";

        std::cout << std::left << std::setw(6) << "ctx" << std::setw(10) << "sessions"
                  << std::setw(16) << "handler_calls" << std::setw(16) << "max_concurrent"
                  << std::setw(10) << "overlap" << std::setw(10) << "affinity" << "\n";
        std::cout << std::string(68, '-') << "\n";

        bool all_ok = true;
        for (int i = 0; i < worker_count_; ++i)
        {
            const auto& s = stats_[i];
            std::cout << std::left << std::setw(6) << i << std::setw(10) << s.sessions.load()
                      << std::setw(16) << s.handler_calls.load() << std::setw(16)
                      << s.max_active.load() << std::setw(10) << s.overlap_violations.load()
                      << std::setw(10) << s.affinity_violations.load() << "\n";

            if (s.sessions.load() != sessions_per_ctx_) all_ok = false;
            if (s.max_active.load() > 1) all_ok = false;
            if (s.overlap_violations.load() != 0) all_ok = false;
            if (s.affinity_violations.load() != 0) all_ok = false;
            if (s.handler_calls.load() == 0) all_ok = false;
        }

        int connected = 0, completed = 0, owned = 0, pushes = 0;
        long long pongs = 0;
        for (const auto& r : results_)
        {
            if (r->connected.load()) ++connected;
            if (r->completed.load()) ++completed;
            if (r->owned.load()) ++owned;
            pongs += r->pongs.load();
            pushes += r->pushes.load();
        }

        std::cout << "\nclient: connected=" << connected << "/" << total_ << "  completed="
                  << completed << "/" << total_ << "\n";
        std::cout << "pongs=" << pongs << "/" << (static_cast<long long>(total_) * rounds_)
                  << "  pushes(server 主动推)=" << pushes << "\n";
        std::cout << "socket owned by target io_context: " << owned << "/" << total_ << "\n";

        if (connected != total_ || completed != total_ || owned != total_
            || pongs != static_cast<long long>(total_) * rounds_)
        {
            all_ok = false;
        }

        std::cout << "\n"
                  << (all_ok ? "PASS" : "FAIL")
                  << ": 每个 io_context 单线程承载多个并发 Session，"
                  << (all_ok ? "handler 全程串行、无重叠、线程亲和正确、数据无错乱"
                             : "存在异常，见上表")
                  << "\n";
        return all_ok;
    }

    int                                         worker_count_;
    int                                         sessions_per_ctx_;
    int                                         rounds_;
    int                                         total_;
    std::vector<CtxStats>                       stats_;
    std::vector<std::unique_ptr<SessionResult>> results_;

    std::vector<std::unique_ptr<boost::asio::io_context>> worker_ctxs_;
    std::vector<std::thread>                              worker_threads_;
    std::vector<std::shared_ptr<Client>>                  clients_;
    std::vector<decltype(makeWorkGuard(std::declval<boost::asio::io_context&>()))> worker_guards_;

    std::atomic<int> done_count_{0};
};

int main(int argc, char** argv)
{
    const int worker_count     = (argc > 1) ? std::atoi(argv[1]) : 4;
    const int sessions_per_ctx = (argc > 2) ? std::atoi(argv[2]) : 20;
    const int rounds           = (argc > 3) ? std::atoi(argv[3]) : 10;

    if (worker_count <= 0 || sessions_per_ctx <= 0 || rounds <= 0)
    {
        std::cerr << "usage: " << argv[0] << " [worker_count] [sessions_per_ctx] [rounds]\n";
        return 2;
    }

    Probe probe(worker_count, sessions_per_ctx, rounds);
    return probe.run() ? 0 : 1;
}

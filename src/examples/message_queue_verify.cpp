// MyConcurrentQueue / MyBlockingConcurrentQueue 行为验证程序
//
// 零依赖版本（同样的用例另有一份 gtest 版，见 src/tests/message_queue_test.cpp），
// 装不了 GoogleTest 也能跑：
//
//     cmake --build build --target message_queue_verify
//     ./build/bin/message_queue_verify [每条生产线程的消息数]
//
// 用例：
//   [1] 阈值弃载：到第 limit 条之后 tryPush 返回 false，腾出一格后可再进
//   [2] FIFO：单线程下进出顺序一致
//   [3] 多生产者守恒：成功入队数 + 被弃载数 == 总尝试数，且取出的条数 == 成功入队数
//   [4] 收尾后深度归零
//   [5] kick 握手双检：复刻 network.cpp 里 is_sending_ 那段协议，验证"入了队就
//       一定有人来发"。丢掉双检的任何一半，都会有消息永久搁在队列里（lost wakeup）
//
// [3] 和 [5] 是并发用例，只有在 ThreadSanitizer 下才有意义：
//     cmake -S src -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON

#include "my_concurrent_queue.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace
{
    int g_failures = 0;

    void report(const char* name, bool ok)
    {
        printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok)
        {
            ++g_failures;
        }
    }

    // [1] 阈值弃载
    bool testLimit()
    {
        cncpp::MyConcurrentQueue<int> q(4);
        for (int i = 0; i < 4; ++i)
        {
            if (!q.tryPush(int(i)))  // tryPush 只接受右值，见 my_concurrent_queue.h
            {
                return false;
            }
        }
        if (q.tryPush(99))
        {
            return false;  // 第 5 条必须被拒
        }
        if (q.size() != 4)
        {
            return false;
        }

        int v = -1;
        if (!q.tryPop(v) || v != 0)
        {
            return false;
        }
        if (!q.tryPush(4))
        {
            return false;  // 腾出一格后可以再进
        }
        return q.limit() == 4;
    }

    // [2] FIFO
    bool testFifo()
    {
        cncpp::MyConcurrentQueue<int> q(128);
        for (int i = 0; i < 100; ++i)
        {
            if (!q.tryPush(int(i)))
            {
                return false;
            }
        }
        for (int i = 0; i < 100; ++i)
        {
            int v = -1;
            if (!q.tryPop(v) || v != i)
            {
                return false;
            }
        }
        int v = -1;
        return !q.tryPop(v);
    }

    // [3]+[4] 多生产者守恒，收尾后深度归零
    bool testMultiProducer(size_t per_thread)
    {
        constexpr int    kProducers = 4;
        constexpr size_t kLimit     = 64;

        cncpp::MyConcurrentQueue<int> q(kLimit);

        std::atomic<uint64_t> accepted{0};
        std::atomic<uint64_t> dropped{0};
        std::atomic<uint64_t> consumed{0};
        std::atomic<bool>     go{false};
        std::atomic<bool>     stop{false};

        std::thread consumer([&] {
            while (!stop.load())
            {
                int v = 0;
                if (q.tryPop(v))
                {
                    consumed.fetch_add(1);
                }
            }
            // 收尾：生产者都已退出，把剩下的取干净
            int v = 0;
            while (q.tryPop(v))
            {
                consumed.fetch_add(1);
            }
        });

        std::vector<std::thread> producers;
        for (int p = 0; p < kProducers; ++p)
        {
            producers.emplace_back([&, p] {
                while (!go.load())
                {
                }
                for (size_t i = 0; i < per_thread; ++i)
                {
                    int v = static_cast<int>(i);
                    if (q.tryPush(std::move(v)))
                    {
                        accepted.fetch_add(1);
                    }
                    else
                    {
                        dropped.fetch_add(1);
                    }
                }
            });
        }

        go.store(true);
        for (auto& t : producers)
        {
            t.join();
        }
        stop.store(true);
        consumer.join();

        const uint64_t attempts = static_cast<uint64_t>(kProducers) * per_thread;
        const bool     ok
            = (accepted.load() + dropped.load() == attempts) && (consumed.load() == accepted.load()) && (q.size() == 0);
        printf("       (accepted=%llu dropped=%llu consumed=%llu)\n", static_cast<unsigned long long>(accepted.load()),
               static_cast<unsigned long long>(dropped.load()), static_cast<unsigned long long>(consumed.load()));
        return ok;
    }

    // [5] kick 握手双检
    //
    // 这一段是 network.cpp 里 Session::send() / processSendQueue() 那个握手的等价
    // 复刻（详见那两处的注释）。容量取 8 是刻意的：队列在空/满之间频繁翻转，
    // 才能让"取空之后、清标志之前"那个窗口被踩中。
    bool testKickHandshake(int producers, size_t per_thread, unsigned seed)
    {
        cncpp::MyConcurrentQueue<int> q(8);

        std::atomic<bool>       sending{false};
        std::atomic<uint64_t>   posts{0};
        std::atomic<uint64_t>   handled{0};
        std::atomic<bool>       stop{false};
        std::mutex              m;
        std::condition_variable cv;

        // 消费端：processSendQueue() 的等价逻辑
        auto drain = [&]() {
            for (;;)
            {
                int  v       = 0;
                bool got_any = false;
                while (q.tryPop(v))
                {
                    got_any = true;
                }

                if (got_any)
                {
                    continue;  // 取到东西就再取一轮
                }

                // 先清标志，再复查 —— 顺序不能反
                sending.store(false);
                if (!q.empty())
                {
                    bool expected = false;
                    if (!sending.compare_exchange_strong(expected, true))
                    {
                        return;  // 生产端已经抢走标志，它会 post，交给它
                    }
                    continue;
                }
                return;
            }
        };

        std::thread consumer([&] {
            for (;;)
            {
                std::unique_lock<std::mutex> lock(m);
                cv.wait(lock, [&] {
                    return handled.load() < posts.load() || stop.load();
                });
                const uint64_t target = posts.load();
                lock.unlock();

                while (handled.load() < target)
                {
                    drain();
                    handled.fetch_add(1);
                }

                if (stop.load() && handled.load() >= posts.load())
                {
                    break;
                }
            }
        });

        std::vector<std::thread> th;
        for (int p = 0; p < producers; ++p)
        {
            th.emplace_back([&, p] {
                std::mt19937 rng(seed + static_cast<unsigned>(p));
                for (size_t i = 0; i < per_thread; ++i)
                {
                    int v = static_cast<int>(p * 1000000 + i);
                    if (!q.tryPush(std::move(v)))
                    {
                        continue;  // 弃载，不计入
                    }

                    bool expected = false;
                    if (sending.compare_exchange_strong(expected, true))
                    {
                        posts.fetch_add(1);
                        cv.notify_one();
                    }

                    // 随机让出，把时序窗口撑开
                    if ((rng() & 0xF) == 0)
                    {
                        std::this_thread::yield();
                    }
                }
            });
        }

        for (auto& t : th)
        {
            t.join();
        }
        stop.store(true);
        cv.notify_all();
        consumer.join();

        // 关键断言：**不额外 drain 一次**就必须是空的。
        // 若握手丢了双检的任何一半，就会有消息搁在队列里没人发 —— 额外 drain
        // 一次会顺手把它取走，正好把 bug 盖住。
        printf("       (posts=%llu handled=%llu stranded=%zu)\n", static_cast<unsigned long long>(posts.load()),
               static_cast<unsigned long long>(handled.load()), q.size());
        return q.empty();
    }

    bool testBlockingQueue()
    {
        cncpp::MyBlockingConcurrentQueue<int> q(4);
        for (int i = 0; i < 4; ++i)
        {
            if (!q.tryPush(int(i)))
            {
                return false;
            }
        }
        if (q.tryPush(99))
        {
            return false;  // 阻塞版的生产端同样弃载，绝不等待
        }
        int v = -1;
        if (!q.tryPop(v) || v != 0)
        {
            return false;
        }
        if (!q.waitPopTimed(v, 1000000) || v != 1)
        {
            return false;
        }
        return q.size() == 2;
    }
}  // namespace

int main(int argc, char** argv)
{
    const size_t per_thread = (argc > 1) ? static_cast<size_t>(std::atoll(argv[1])) : 20000;

    printf("MyConcurrentQueue 验证（每条生产线程 %zu 条）\n", per_thread);
    report("[1] 阈值弃载", testLimit());
    report("[2] FIFO", testFifo());
    report("[3][4] 多生产者守恒 + 深度归零", testMultiProducer(per_thread));
    report("[5] kick 握手双检（无 stranded 消息）", testKickHandshake(4, per_thread / 4 + 1, 20260101u));
    report("[6] 阻塞版：生产端同样弃载", testBlockingQueue());

    if (g_failures != 0)
    {
        printf("\n%d 项失败\n", g_failures);
        return 1;
    }
    printf("\n全部通过\n");
    return 0;
}

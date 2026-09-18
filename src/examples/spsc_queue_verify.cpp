// SpscMessageQueue 行为验证程序
//
// 目的：把 ADR-0004 里那条"编译器检查不了"的前提钉死 —— 一边一个线程时，
// 队列必须既不丢消息、也不重复交付、也不错序。
//
// [1]~[4] 单线程就能验；[5] 才是关键，但它**只有在 TSAN 下跑才有意义**：
// 生产者写槽位与消费者读槽位之间没有任何锁，靠的全是 write_index_ 的
// release 与 read_index_ 的 acquire。内存序写错时单线程用例照样全绿。
//
// 跑法（开关见 cmake/compiler_options.cmake 的 ENABLE_TSAN）：
//     cmake -S src -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON
//     cmake --build build --target spsc_queue_verify
//     ./build/bin/spsc_queue_verify [消息条数]
//
// 想验 [4] 的"是否真的释放"要换 ASAN/LSAN：这两个 sanitizer 与 TSAN 互斥，
// 得单独配一次构建：CMAKE_CXX_FLAGS="-fsanitize=address,leaks -g"
//
// 同样的用例另有一份 gtest 版本（src/tests/spsc_queue_test.cpp，需 -DENABLE_TESTS=ON）。
// 留着这一份是因为它零外部依赖：装不了 GoogleTest 的环境照样能跑、
// 也照样能套 TSAN。两份逻辑一致，改了一处另一处要跟着改。
//
// 退出码 0 = 全部通过，1 = 存在失败项

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <thread>

#include "spsc_message_queue.h"

using namespace cncpp;

namespace
{
    int g_failed = 0;

    void verdict(bool ok)
    {
        printf("  %s\n", ok ? "OK" : "FAIL");
        if (!ok)
        {
            ++g_failed;
        }
    }

    // 每条消息用它自己的序号当载荷：内容对得上，就说明既没串行到别的槽位，
    // 也没漏 unique_ptr 载荷的析构与重建
    std::string bodyOf(size_t seq)
    {
        return "seq-" + std::to_string(seq);
    }

    // ------------------------------------------------------------ [1] 基本约定

    void caseFifoAndBounds()
    {
        printf("\n[1] 单线程：FIFO 顺序 / 满时 false / 空时 false\n");

        SpscMessageQueue queue(4);
        bool             ok = queue.capacity() == 4 && queue.empty() && queue.size() == 0;

        for (size_t i = 0; i < 4; ++i)
        {
            NetworkMessage msg;
            msg.setBody(bodyOf(i));
            ok = queue.push(std::move(msg)) && ok;
        }

        // 满时必须返回 false，且源消息必须原封不动。push 的注释承诺了这一点，
        // io 线程在队列满时原地重推全靠它
        NetworkMessage rejected;
        rejected.setBody("must-survive");
        const bool         push_rejected = queue.push(std::move(rejected));
        const std::string* kept          = rejected.getBody();
        ok                               = !push_rejected && kept != nullptr && *kept == "must-survive" && ok;

        for (size_t i = 0; i < 4; ++i)
        {
            NetworkMessage     out;
            const bool         popped = queue.pop(out);
            const std::string* body   = out.getBody();
            ok                        = popped && body != nullptr && *body == bodyOf(i) && ok;
        }

        NetworkMessage drained;
        ok = !queue.pop(drained) && ok;

        verdict(ok);
    }

    // ------------------------------------------------------------ [2] 回绕

    void caseWraparound()
    {
        printf("\n[2] 单线程：反复回绕后槽位内容仍然正确\n");

        // 3 不是 2 的幂，逼 "write % capacity" 频繁回绕，
        // 也逼同一块槽位上的 unique_ptr 载荷反复析构再重建
        const size_t kCapacity = 3;
        const int    kRounds   = 10000;

        SpscMessageQueue queue(kCapacity);
        bool             ok = true;

        for (int round = 0; round < kRounds && ok; ++round)
        {
            for (size_t i = 0; i < kCapacity; ++i)
            {
                NetworkMessage msg;
                msg.setBody(std::to_string(round) + ":" + std::to_string(i));
                ok = queue.push(std::move(msg)) && ok;
            }
            for (size_t i = 0; i < kCapacity; ++i)
            {
                NetworkMessage     out;
                const std::string  expect = std::to_string(round) + ":" + std::to_string(i);
                const bool         popped = queue.pop(out);
                const std::string* body   = out.getBody();
                ok                        = popped && body != nullptr && *body == expect && ok;
            }
        }

        verdict(ok);
    }

    // ------------------------------------------------------------ [3] 推取不等

    void caseRagged()
    {
        printf("\n[3] 单线程：推/取次数不等时的占用与顺序\n");

        const size_t kCapacity = 8;
        // {推, 取}：故意让队列处于半满、刚回绕、刚抽干等各种形态
        const int pattern[][2] = {
            {3, 1},
            {1, 3},
            {4, 0},
            {0, 4},
            {2, 2},
            {5, 5}
        };
        const size_t kPatternCount = sizeof(pattern) / sizeof(pattern[0]);

        SpscMessageQueue        queue(kCapacity);
        std::deque<std::string> expected;  // 参考实现：应当还在队列里的那些载荷
        bool                    ok  = true;
        size_t                  seq = 0;

        for (int round = 0; round < 2000 && ok; ++round)
        {
            const int* p = pattern[round % static_cast<int>(kPatternCount)];

            for (int i = 0; i < p[0] && ok; ++i)
            {
                if (expected.size() >= kCapacity)
                {
                    break;  // 满了就少推几条，不算错
                }
                const std::string body = bodyOf(seq++);
                NetworkMessage    msg;
                msg.setBody(body);
                ok = queue.push(std::move(msg)) && ok;
                expected.push_back(body);
            }

            for (int i = 0; i < p[1] && ok; ++i)
            {
                NetworkMessage out;
                const bool     popped = queue.pop(out);

                if (expected.empty())
                {
                    ok = !popped && ok;  // 应当是空的，pop 必须失败
                    continue;
                }

                const std::string* body = out.getBody();
                ok                      = popped && body != nullptr && *body == expected.front() && ok;
                expected.pop_front();
            }
        }

        ok = queue.size() == expected.size() && ok;
        verdict(ok);
    }

    // ------------------------------------------------------------ [4] 残留释放

    void caseDestructorReleasesLeftovers()
    {
        printf("\n[4] 析构：队列里还留着消息（是否真的释放由 ASAN/LSAN 判定，见文件头）\n");

        SpscMessageQueue queue(4);
        for (int i = 0; i < 4; ++i)
        {
            NetworkMessage msg;
            msg.setBody(std::string(4096, 'x'));
            (void)queue.push(std::move(msg));
        }

        // 真正的检查发生在这个作用域结束时：vector<NetworkMessage> 逐个析构，
        // 载荷的 unique_ptr 必须跟着释放
        verdict(queue.size() == 4);
    }

    // ------------------------------------------------------------ [5] 双线程

    void caseTwoThreads(size_t total)
    {
        printf("\n[5] 双线程：%llu 条消息必须不丢、不重、不错序\n", static_cast<unsigned long long>(total));

        // 容量取 8：让槽位在两边频繁易手，最大化踩中竞争窗口的概率
        SpscMessageQueue queue(8);

        std::atomic<bool> stop(false);
        size_t            consumed   = 0;
        bool              mismatched = false;

        std::thread consumer([&]() {
            NetworkMessage out;
            size_t         next = 0;
            while (next < total)
            {
                if (!queue.pop(out))
                {
                    std::this_thread::yield();
                    continue;
                }
                const std::string* body = out.getBody();
                if (body == nullptr || *body != bodyOf(next))
                {
                    mismatched = true;
                    consumed   = next;
                    stop.store(true, std::memory_order_relaxed);
                    return;
                }
                ++next;
            }
            consumed = next;
        });

        for (size_t i = 0; i < total && !stop.load(std::memory_order_relaxed); ++i)
        {
            NetworkMessage msg;
            msg.setBody(bodyOf(i));
            // push 失败时源消息必须还在（[1] 验过），这里正是靠这一点原地重推
            while (!queue.push(std::move(msg)) && !stop.load(std::memory_order_relaxed))
            {
                std::this_thread::yield();
            }
        }

        consumer.join();

        const bool ok = !mismatched && consumed == total;
        if (!ok)
        {
            printf("    消费到 %llu / %llu%s\n", static_cast<unsigned long long>(consumed),
                   static_cast<unsigned long long>(total), mismatched ? "（内容或顺序不符）" : "");
        }
        verdict(ok);
    }

}  // namespace

int main(int argc, char** argv)
{
    // TSAN 下比裸跑慢一个量级，默认条数按 TSAN 能接受的时间取的
    size_t stress_total = 200000;
    if (argc > 1)
    {
        stress_total = static_cast<size_t>(std::strtoull(argv[1], nullptr, 10));
    }

    printf("SpscMessageQueue 验证\n");

    caseFifoAndBounds();
    caseWraparound();
    caseRagged();
    caseDestructorReleasesLeftovers();
    caseTwoThreads(stress_total);

    printf("\n%s  失败项=%d\n", g_failed == 0 ? "全部通过" : "存在失败", g_failed);
    return g_failed == 0 ? 0 : 1;
}

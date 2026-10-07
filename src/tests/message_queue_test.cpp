// MyConcurrentQueue / MyBlockingConcurrentQueue 单元测试
//
// 目的：钉住**我们自己加的那一层**（入队侧阈值与弃载），不是去测 moodycamel
// 本身 —— 那是别人测过的库。ADR-0010 里"为什么界放在库外面"的理由能否成立，
// 全看这几条用例。
//
// 并发用例**只有在 TSAN 下跑才有意义**：
//
//     cmake -S src -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_TSAN=ON
//     cmake --build build --target message_queue_test
//     ./build/bin/message_queue_test
//
// TSAN 检出竞争时会让进程以 66 退出，所以哪怕 gtest 报告全绿，ctest 也照样判
// 失败 —— 这正是我们想要的判定方式。
//
// 另有零依赖版本（无需 GoogleTest，默认参与构建）：src/examples/message_queue_verify.cpp，
// 那边还多一条 kick 握手双检的用例（对应 network.cpp 的 is_sending_）。

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "message.h"
#include "my_concurrent_queue.h"

using namespace cncpp;

namespace
{
    std::string bodyOf(size_t seq)
    {
        return "seq-" + std::to_string(seq);
    }

    const std::string* body(const NetworkMessage& message)
    {
        return message.getBody();
    }

    NetworkMessage makeMessage(size_t seq)
    {
        NetworkMessage message;
        message.setBody(bodyOf(seq));
        return message;
    }
}  // namespace

// --------------------------------------------------------------- 基本约定

TEST(MyConcurrentQueue, ConstructsEmptyAndReportsLimit)
{
    MyConcurrentQueue<NetworkMessage> queue(4);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_EQ(queue.limit(), 4u);
}

TEST(MyConcurrentQueue, TryPopReturnsFalseWhenEmpty)
{
    MyConcurrentQueue<NetworkMessage> queue(4);
    NetworkMessage                    out;
    EXPECT_FALSE(queue.tryPop(out));
}

// --------------------------------------------------------------- 阈值与弃载

// 到阈值后必须拒收，且**源对象完好未被移动** —— 调用方要靠这一点去计数告警，
// 它拿到的是一个还能用的消息（哪怕它选择丢掉）。
TEST(MyConcurrentQueue, TryPushOnFullFailsAndKeepsSourceIntact)
{
    for (size_t limit : {size_t(1), size_t(2), size_t(8), size_t(64)})
    {
        MyConcurrentQueue<NetworkMessage> queue(limit);
        for (size_t i = 0; i < limit; ++i)
        {
            ASSERT_TRUE(queue.tryPush(makeMessage(i))) << "limit=" << limit;
        }

        NetworkMessage extra = makeMessage(999);
        EXPECT_FALSE(queue.tryPush(std::move(extra)));
        ASSERT_TRUE(body(extra) != nullptr);
        EXPECT_EQ(*body(extra), bodyOf(999));

        // 腾出一格之后就必须能再进：阈值是"当前深度"的闸门，不是一次性额度
        NetworkMessage out;
        ASSERT_TRUE(queue.tryPop(out));
        EXPECT_TRUE(queue.tryPush(makeMessage(1000)));
    }
}

TEST(MyConcurrentQueue, PreservesFifoOrder)
{
    MyConcurrentQueue<NetworkMessage> queue(200);
    for (size_t i = 0; i < 200; ++i)
    {
        ASSERT_TRUE(queue.tryPush(makeMessage(i)));
    }

    for (size_t i = 0; i < 200; ++i)
    {
        NetworkMessage out;
        ASSERT_TRUE(queue.tryPop(out));
        ASSERT_TRUE(body(out) != nullptr);
        EXPECT_EQ(*body(out), bodyOf(i));
    }

    NetworkMessage out;
    EXPECT_FALSE(queue.tryPop(out));
    EXPECT_EQ(queue.size(), 0u);
}

// --------------------------------------------------------------- 并发

// 多生产者下的守恒：尝试数 == 入队成功数 + 被弃载数，且取出的 == 入队成功的。
// 这条对应发送侧的真实形状（tick 线程 + 任意业务线程一起 enqueue）。
TEST(MyConcurrentQueue, MultiProducerConservesEveryAcceptedMessage)
{
    constexpr int   kProducers = 4;
    constexpr size_t kPerThread = 5000;
    constexpr size_t kLimit     = 64;

    MyConcurrentQueue<int> queue(kLimit);

    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> dropped{0};
    std::atomic<uint64_t> consumed{0};
    std::atomic<bool>     go{false};
    std::atomic<bool>     stop{false};

    std::thread consumer([&] {
        while (!stop.load())
        {
            int v = 0;
            if (queue.tryPop(v))
            {
                consumed.fetch_add(1);
            }
        }
        int v = 0;
        while (queue.tryPop(v))
        {
            consumed.fetch_add(1);
        }
    });

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back([&] {
            while (!go.load())
            {
            }
            for (size_t i = 0; i < kPerThread; ++i)
            {
                int v = static_cast<int>(i);
                if (queue.tryPush(std::move(v)))
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

    EXPECT_EQ(accepted.load() + dropped.load(), uint64_t(kProducers) * kPerThread);
    EXPECT_EQ(consumed.load(), accepted.load());

    // 深度必须回到 0：CAS 循环里"入队失败要还回配额"那条路径写错的话，
    // 这里会留下一个永久的假占用，队列会一点点被卡死。
    EXPECT_EQ(queue.size(), 0u);
}

// --------------------------------------------------------------- 阻塞版

// 阻塞版的**生产端**语义必须与非阻塞版一致：到阈值照样弃载，绝不等待。
// 阻塞只发生在消费端（waitPop / waitPopTimed），原因见 my_concurrent_queue.h。
TEST(MyBlockingConcurrentQueue, ProducerShedsLoadInsteadOfBlocking)
{
    MyBlockingConcurrentQueue<int> queue(4);
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(queue.tryPush(int(i)));  // tryPush 只接受右值，见 my_concurrent_queue.h
    }
    EXPECT_FALSE(queue.tryPush(99));

    int v = -1;
    EXPECT_TRUE(queue.tryPop(v));
    EXPECT_EQ(v, 0);

    // 消费端可以等：队列里有东西，限时等待必须立刻拿到
    EXPECT_TRUE(queue.waitPopTimed(v, 1000000));
    EXPECT_EQ(v, 1);

    EXPECT_EQ(queue.size(), 2u);
}

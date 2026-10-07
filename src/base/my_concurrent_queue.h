#ifndef MY_CONCURRENT_QUEUE_H
#define MY_CONCURRENT_QUEUE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <cameron314/blockingconcurrentqueue.h>
#include <cameron314/concurrentqueue.h>

namespace cncpp
{
    // 入队侧有界的无锁队列，每条 Session 一个（ADR-0004 的噪声邻居隔离）。
    //
    // 底层是 moodycamel::ConcurrentQueue —— 它天生**无界**（按需分配 block，
    // try_enqueue 只在内存耗尽时才失败）。"界"是我们在入队侧自己加的：
    // depth_ 到了 limit_ 就丢弃，绝不阻塞。为什么界要放在库外面，见 ADR-0010，
    // 两条理由：
    //   1. 无界是 moodycamel 的设计前提，它的构造参数 minCapacity 只是"预分配
    //      多少槽"，不是硬性上界，改不了它的无界本质；
    //   2. 外部阈值顺带锁住了高水位 —— moodycamel 的 block 只回收进内部 free
    //      list、**从不归还系统**，不设闸的话涨过一次就永久占着那些槽位。
    //
    // 满时 tryPush 返回 false 且 item 完好未被移动，由调用方决定怎么处置。
    // 本项目两侧都是弃载 + 计数 + 节流告警（ADR-0004）。
    //
    // **绝不在生产者侧等待**：接收侧的生产者是 io 线程，在 io 线程上等待等于
    // 让所有连接一起停摆（ADR-0004）。
    template <typename T>
    class MyConcurrentQueue
    {
    public:
        explicit MyConcurrentQueue(size_t limit);

        MyConcurrentQueue(const MyConcurrentQueue&)            = delete;
        MyConcurrentQueue& operator=(const MyConcurrentQueue&) = delete;

        // 生产者侧。到阈值则返回 false，此时 item 未被移动 —— 调用方拿到的是一个
        // 还能用的消息（哪怕它选择丢掉），这是它能去计数告警的前提。
        //
        // 只接受右值，**故意不提供 const T& 重载**：NetworkMessage 的拷贝是
        // clone() 深拷贝，多一个左值重载就等于埋一个"写起来顺手、跑起来每条消息
        // 多一次深拷贝"的坑。装平凡类型（int 之类）时用 tryPush(int(i)) 造个临时值。
        bool tryPush(T&& item);

        // 消费者侧。空则返回 false。false 只表示"这一刻没有消息"，
        // 不代表连接关了 —— end-of-stream 是 Session 的事，不由队列表达
        // （ADR-0004：早先让 pop 在停止且空时返回 true，配合消费端一律写的
        //  while (pop(message))，连接一关就是死循环）。
        bool tryPop(T& out);

        // 某一瞬间的深度。只用于日志与监控，不要拿它做同步判断。
        size_t size() const;

        bool   empty() const;
        size_t limit() const;

    private:
        // 入队侧阈值。构造后只读：项目没有配置热更新通道（signal_handler.cpp
        // 的 SIGHUP 还是个空 TODO），改配置要重启。做成可写会引出两个说不清的
        // 语义 —— 调小时存量怎么办、以及调小并不释放内存（见 ADR-0010）。
        const size_t limit_;

        // 当前深度。relaxed 就够：它只是个准入计数，不承载任何数据可见性，
        // 数据本身的同步由 ConcurrentQueue 自己保证。
        std::atomic<size_t> depth_;

        moodycamel::ConcurrentQueue<T> queue_;
    };

    template <typename T>
    inline MyConcurrentQueue<T>::MyConcurrentQueue(size_t limit) : limit_(limit), depth_(0)
    {
    }

    template <typename T>
    inline bool MyConcurrentQueue<T>::tryPush(T&& item)
    {
        // CAS 循环而不是 load 判一次再 ++：发送侧是真多生产者（tick 线程 + 任意
        // 业务线程），load 判过之后可能被别人抢先，直接自增会越过阈值。
        // 接收侧虽然只有一个生产者，用同一份实现省得两边语义漂移。
        size_t depth = depth_.load(std::memory_order_relaxed);
        do
        {
            if (depth >= limit_)
            {
                return false;
            }
        } while (!depth_.compare_exchange_weak(depth, depth + 1, std::memory_order_relaxed));

        if (!queue_.try_enqueue(std::move(item)))
        {
            // 无界队列走到这里只可能是内存耗尽。配额必须还回去：否则这条消息
            // 永远不会被消费、depth_ 却永久 +1，队列会被"假占用"一点点卡死。
            depth_.fetch_sub(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    template <typename T>
    inline bool MyConcurrentQueue<T>::tryPop(T& out)
    {
        if (!queue_.try_dequeue(out))
        {
            return false;
        }
        depth_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    template <typename T>
    inline size_t MyConcurrentQueue<T>::size() const
    {
        return depth_.load(std::memory_order_relaxed);
    }

    template <typename T>
    inline bool MyConcurrentQueue<T>::empty() const
    {
        return size() == 0;
    }

    template <typename T>
    inline size_t MyConcurrentQueue<T>::limit() const
    {
        return limit_;
    }

    // 消费端可阻塞的版本，底层是 moodycamel::BlockingConcurrentQueue。
    //
    // 阻塞只发生在**消费端**：waitPop / waitPopTimed 会等消息到来。生产端仍然
    // 是 tryPush，到阈值就弃载、**绝不等待** —— ADR-0004 禁止生产者在 io 线程
    // 上等待。也就是说这两个类的差别只在"没消息时消费端干不干等"，限流语义
    // 完全一致。
    //
    // 入队侧的界同样用外部计数，不用 BlockingConcurrentQueue 自己的容量参数：
    // 那个构造签名在引入时未能核实，而外部计数与 MyConcurrentQueue 逐字相同，
    // 两个类的限流行为不会漂移。
    //
    // **当前没有调用点**（ADR-0010）。主干不用它是因为主循环由定时器驱动而不
    // 是由消息驱动（ADR-0001）：让 tick 线程 wait 在没有消息到达时会让时间轮
    // 停摆，而时间轮不做补拍，会整体走慢且再也追不回来。保留它是为将来"专用
    // 消费线程"的场景准备的 —— 别因为它现在没人用就当死代码删掉，也别在 io
    // 线程上用它的等待接口。
    template <typename T>
    class MyBlockingConcurrentQueue
    {
    public:
        explicit MyBlockingConcurrentQueue(size_t limit);

        MyBlockingConcurrentQueue(const MyBlockingConcurrentQueue&)            = delete;
        MyBlockingConcurrentQueue& operator=(const MyBlockingConcurrentQueue&) = delete;

        // 生产者侧，语义与 MyConcurrentQueue::tryPush 完全一致。
        bool tryPush(T&& item);

        bool tryPop(T& out);

        // 消费端阻塞等待。没有消息就一直等，直到有消息或队列被唤醒。
        void waitPop(T& out);

        // 消费端限时等待，超时返回 false。timeout_us 是微秒。
        bool waitPopTimed(T& out, std::int64_t timeout_us);

        size_t size() const;
        bool   empty() const;
        size_t limit() const;

    private:
        const size_t limit_;

        std::atomic<size_t> depth_;

        moodycamel::BlockingConcurrentQueue<T> queue_;
    };

    template <typename T>
    inline MyBlockingConcurrentQueue<T>::MyBlockingConcurrentQueue(size_t limit) : limit_(limit), depth_(0)
    {
    }

    template <typename T>
    inline bool MyBlockingConcurrentQueue<T>::tryPush(T&& item)
    {
        size_t depth = depth_.load(std::memory_order_relaxed);
        do
        {
            if (depth >= limit_)
            {
                return false;
            }
        } while (!depth_.compare_exchange_weak(depth, depth + 1, std::memory_order_relaxed));

        if (!queue_.try_enqueue(std::move(item)))
        {
            depth_.fetch_sub(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    template <typename T>
    inline bool MyBlockingConcurrentQueue<T>::tryPop(T& out)
    {
        if (!queue_.try_dequeue(out))
        {
            return false;
        }
        depth_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    template <typename T>
    inline void MyBlockingConcurrentQueue<T>::waitPop(T& out)
    {
        queue_.wait_dequeue(out);
        depth_.fetch_sub(1, std::memory_order_relaxed);
    }

    template <typename T>
    inline bool MyBlockingConcurrentQueue<T>::waitPopTimed(T& out, std::int64_t timeout_us)
    {
        if (!queue_.wait_dequeue_timed(out, timeout_us))
        {
            return false;
        }
        depth_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    template <typename T>
    inline size_t MyBlockingConcurrentQueue<T>::size() const
    {
        return depth_.load(std::memory_order_relaxed);
    }

    template <typename T>
    inline bool MyBlockingConcurrentQueue<T>::empty() const
    {
        return size() == 0;
    }

    template <typename T>
    inline size_t MyBlockingConcurrentQueue<T>::limit() const
    {
        return limit_;
    }

}  // namespace cncpp

#endif  // MY_CONCURRENT_QUEUE_H

#ifndef SPSC_MESSAGE_QUEUE_H
#define SPSC_MESSAGE_QUEUE_H

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "message.h"

namespace cncpp
{
    // 单生产者单消费者（SPSC）有界队列，每条连接一个。
    //
    // 生产者是 io 线程（Session 拆帧、解码后投递），消费者是 tick 线程。
    // 这个前提不由编译器检查：多出一个生产者或消费者不会有编译错误，只会有数据竞争。
    // 见 ADR-0004 —— 当前 io_context 的用法保证了两边各只有一个线程。
    //
    // 队列满时 push 返回 false，由调用方决定怎么处理，**绝不阻塞**。
    // 生产者在 io 线程上，在那里等待等于让所有连接一起停摆。
    class SpscMessageQueue
    {
    public:
        // 每个 Session 构造时就按 capacity 预分配满额槽位，所以这个值直接决定
        // 每条连接的固定内存开销（约 capacity × sizeof(NetworkMessage)）。
        // 256 条缓冲意味着单连接能扛住远超一拍的突发；真到持续跟不上时，
        // 再大的容量也只是把丢消息推迟一会儿。
        static constexpr size_t kDefaultCapacity = 256;

        explicit SpscMessageQueue(size_t capacity = kDefaultCapacity);

        SpscMessageQueue(const SpscMessageQueue&)            = delete;
        SpscMessageQueue& operator=(const SpscMessageQueue&) = delete;

        // 生产者侧。满则返回 false，此时 message 完好未被移动，调用方可以另作处理。
        bool push(NetworkMessage&& message);

        // 消费者侧。空则返回 false。false 只表示"这一刻没有消息"，
        // 不代表连接关了——end-of-stream 是 Session 的事，不由队列表达。
        bool pop(NetworkMessage& out);

        // 某一瞬间的近似长度。两个下标是分别读的，并发下它可能瞬时失真，
        // 只用于日志与监控，不要拿它做同步判断。
        size_t size() const;

        bool   empty() const;
        size_t capacity() const;

    private:
        // 生产者与消费者的下标各占一条 cache line。挤在同一条上会让两边互相
        // 把对方的 cache line 打脏，队列退化成比加锁还慢。
        static constexpr size_t kCacheLineSize = 64;

        // 生产者独占：单调递增，永不回绕。用单调递增而非取模后的环形下标，
        // 是为了让"满"和"空"天然可区分（差值等于容量 / 差值等于 0），
        // 不用像经典环形缓冲那样空一格出来。
        alignas(kCacheLineSize) std::atomic<size_t> write_index_;
        // 生产者缓存的消费者进度，避免每次 push 都去读对端那条 cache line。
        alignas(kCacheLineSize) size_t cached_read_index_;

        // 消费者独占。
        alignas(kCacheLineSize) std::atomic<size_t> read_index_;
        alignas(kCacheLineSize) size_t cached_write_index_;

        std::vector<NetworkMessage> buffer_;
    };

    inline SpscMessageQueue::SpscMessageQueue(size_t capacity)
        : write_index_(0),
          cached_read_index_(0),
          read_index_(0),
          cached_write_index_(0),
          buffer_(capacity)
    {
        if (capacity == 0)
        {
            throw std::invalid_argument("SpscMessageQueue capacity must be greater than zero");
        }
    }

    inline bool SpscMessageQueue::push(NetworkMessage&& message)
    {
        const size_t capacity = buffer_.size();
        const size_t write    = write_index_.load(std::memory_order_relaxed);

        if (write - cached_read_index_ >= capacity)
        {
            // 可能是真满了，也可能只是还没看到消费者的最新进度。重读一次再判。
            // acquire 与消费者 release 式的下标发布配对：拿到它，消费者对这个槽位
            // 的移动就已经完成，写进去才是安全的。
            cached_read_index_ = read_index_.load(std::memory_order_acquire);
            if (write - cached_read_index_ >= capacity)
            {
                return false;
            }
        }

        buffer_[write % capacity] = std::move(message);

        // release：先于它的槽位写入必须对 acquire 到这个下标的消费者可见。
        write_index_.store(write + 1, std::memory_order_release);
        return true;
    }

    inline bool SpscMessageQueue::pop(NetworkMessage& out)
    {
        const size_t capacity = buffer_.size();
        const size_t read     = read_index_.load(std::memory_order_relaxed);

        if (read == cached_write_index_)
        {
            // 同上：可能是真空，也可能只是生产者还没把进度发布出来。
            cached_write_index_ = write_index_.load(std::memory_order_acquire);
            if (read == cached_write_index_)
            {
                return false;
            }
        }

        out = std::move(buffer_[read % capacity]);

        // release：消费者对槽位的读取完成后才发布，生产者据此知道这个槽可以重写。
        read_index_.store(read + 1, std::memory_order_release);
        return true;
    }

    inline size_t SpscMessageQueue::size() const
    {
        const size_t write = write_index_.load(std::memory_order_acquire);
        const size_t read  = read_index_.load(std::memory_order_acquire);

        // read 不会超过 write，所以反常只需要兜住并发读导致的瞬时错序。
        return write >= read ? write - read : 0;
    }

    inline bool SpscMessageQueue::empty() const
    {
        return size() == 0;
    }

    inline size_t SpscMessageQueue::capacity() const
    {
        return buffer_.size();
    }

}  // namespace cncpp

#endif  // SPSC_MESSAGE_QUEUE_H

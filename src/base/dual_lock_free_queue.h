#ifndef DUAL_LOCK_FREE_QUEUE_H
#define DUAL_LOCK_FREE_QUEUE_H

#include <atomic>
#include <memory>

#include "message.h"

namespace cncpp
{
    class LockFreeQueue
    {
    public:
        LockFreeQueue(size_t capacity = 4096)
            : capacity_(capacity), buffer_(new NetworkMessage[capacity]), head_(0), tail_(0)
        {
        }

        bool push(const NetworkMessage& message)
        {
            size_t current_tail = tail_.load(std::memory_order_relaxed);
            size_t next_tail    = (current_tail + 1) % capacity_;

            if (next_tail == head_.load(std::memory_order_acquire))
            {
                return false;  // 队列已满
            }

            buffer_[current_tail] = message;
            tail_.store(next_tail, std::memory_order_release);
            return true;
        }

        bool pop(NetworkMessage& message)
        {
            size_t current_head = head_.load(std::memory_order_relaxed);

            if (current_head == tail_.load(std::memory_order_acquire))
            {
                return false;  // 队列为空
            }

            message = buffer_[current_head];
            head_.store((current_head + 1) % capacity_, std::memory_order_release);
            return true;
        }

        size_t size() const
        {
            size_t current_head = head_.load(std::memory_order_acquire);
            size_t current_tail = tail_.load(std::memory_order_acquire);

            if (current_tail >= current_head)
            {
                return current_tail - current_head;
            }
            else
            {
                return capacity_ - current_head + current_tail;
            }
        }

        void clear()
        {
            head_.store(tail_.load(std::memory_order_acquire), std::memory_order_release);
        }

    private:
        size_t                            capacity_;
        std::unique_ptr<NetworkMessage[]> buffer_;
        std::atomic<size_t>               head_;
        std::atomic<size_t>               tail_;
    };

    class DualLockFreeQueue
    {
    public:
        DualLockFreeQueue(size_t capacity = 4096)
            : write_queue_(std::make_unique<LockFreeQueue>(capacity)),
              read_queue_(std::make_unique<LockFreeQueue>(capacity)),
              stopped_(false),
              switch_threshold_(1024)
        {
        }

        void push(const NetworkMessage& message)
        {
            // 向当前写入队列推送消息
            while (!write_queue_->push(message))
            {
                // 写入队列已满，尝试切换队列
                switchQueues();
            }

            // 检查是否需要切换队列
            if (write_queue_->size() >= switch_threshold_)
            {
                switchQueues();
            }
        }

        bool pop(NetworkMessage& message)
        {
            // 从当前读取队列读取消息
            if (read_queue_->pop(message))
            {
                return true;
            }

            // 如果读取队列为空，尝试切换队列
            switchQueues();

            // 再次尝试读取
            if (read_queue_->pop(message))
            {
                return true;
            }

            // 队列已停止
            return stopped_;
        }

        void stop()
        {
            stopped_ = true;
        }

    private:
        void switchQueues()
        {
            // 交换读写队列
            std::swap(write_queue_, read_queue_);
            // 重置新的写入队列
            write_queue_->clear();
        }

        std::unique_ptr<LockFreeQueue> write_queue_;
        std::unique_ptr<LockFreeQueue> read_queue_;
        std::atomic<bool>              stopped_;
        size_t                         switch_threshold_;
    };

}  // namespace cncpp

#endif  // DUAL_LOCK_FREE_QUEUE_H

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

#if 0
namespace cncpp
{
    // 定时器精度级别
    enum class TimerPrecision
    {
        MILLISECOND,  // 毫秒级
        SECOND,       // 秒级
        MINUTE,       // 分钟级
        HOUR,         // 小时级
        DAY           // 天级
    };

    // 定时器模式
    enum class TimerMode
    {
        ONCE,   // 单次定时器
        REPEAT  // 循环定时器
    };

    // 前向声明
    class TimerManager;

    // 定时器节点
    struct TimerNode
    {
        uint64_t expire_;    // 到期时间（毫秒）
        uint64_t interval_;  // 间隔时间（毫秒，用于循环定时器）
        uint32_t sequence_;  // 序列号，用于区分相同到期时间的定时器
        TimerMode mode_;     // 定时器模式
        bool cancelled_;     // 是否已取消

        std::function<void()> callback_;  // 回调函数

        TimerNode* next_;  // 链表下一个节点

        TimerNode() : expire_(0), interval_(0), sequence_(0), mode_(TimerMode::ONCE), cancelled_(false), next_(nullptr)
        {
        }
    };

    // 时间轮
    class TimerWheel
    {
    public:
        TimerWheel(int slot_count, uint64_t slot_interval);
        ~TimerWheel();

        // 禁止拷贝和移动
        TimerWheel(const TimerWheel&) = delete;
        TimerWheel& operator=(const TimerWheel&) = delete;

        // 添加定时器到当前槽位
        void AddTimer(TimerNode* node);

        // 触发当前槽位的所有定时器
        void Trigger(int current_slot);

        // 获取槽位间隔（毫秒）
        uint64_t GetSlotInterval() const
        {
            return slot_interval_;
        }

    private:
        int slot_count_;                            // 槽位数量
        uint64_t slot_interval_;                    // 槽位间隔（毫秒）
        std::vector<std::list<TimerNode*>> slots_;  // 槽位链表
    };

    // 定时器管理器
    class TimerManager : public Singleton<TimerManager>
    {
        friend class Singleton<TimerManager>;

    public:
        // 初始化定时器管理器
        bool Initialize(uint32_t pool_size = 1024);

        // 添加定时器
        // delay_ms: 延迟时间（毫秒）
        // callback: 回调函数
        // mode: 定时器模式（单次/循环）
        // returns: 定时器节点指针，用于取消定时器
        TimerNode* AddTimer(uint64_t delay_ms, std::function<void()> callback, TimerMode mode = TimerMode::ONCE);

        // 添加循环定时器
        // interval_ms: 间隔时间（毫秒）
        // callback: 回调函数
        // returns: 定时器节点指针，用于取消定时器
        TimerNode* AddRepeatTimer(uint64_t interval_ms, std::function<void()> callback);

        // 取消定时器
        void RemoveTimer(TimerNode* node);

        // 更新定时器（需要定期调用，建议每毫秒调用一次）
        void Update();

        // 获取当前时间（毫秒）
        uint64_t GetNow() const;

        // 获取启动后经过的时间（毫秒）
        uint64_t GetElapsed() const;

        // 获取定时器总数
        size_t GetTimerCount() const
        {
            return timer_count_.load();
        }

        // 清理所有定时器
        void Clear();

        // 获取精度
        TimerPrecision GetPrecision() const
        {
            return precision_;
        }

    private:
        TimerManager();
        ~TimerManager();

        // 禁止拷贝和移动
        TimerManager(const TimerManager&) = delete;
        TimerManager& operator=(const TimerManager&) = delete;

        // 分配定时器节点
        TimerNode* AllocNode();

        // 释放定时器节点
        void FreeNode(TimerNode* node);

        // 将定时器添加到时间轮
        void AddToWheel(TimerNode* node);

        // 触发到期的定时器
        void TriggerTimers(std::list<TimerNode*>& expired_timers);

        // 清理已取消的定时器
        void CleanupCancelled();

    private:
        // 时间轮配置
        static constexpr int MILLISECOND_SLOTS = 64;  // 毫秒级：64个槽位，每槽约15.6ms
        static constexpr int SECOND_SLOTS = 64;       // 秒级：64个槽位，每槽约1s
        static constexpr int MINUTE_SLOTS = 64;       // 分钟级：64个槽位，每槽约1min
        static constexpr int HOUR_SLOTS = 32;         // 小时级：32个槽位，每槽约1h
        static constexpr int DAY_SLOTS = 32;          // 天级：32个槽位，每槽约1天

        // 精度配置
        static constexpr uint64_t MILLISECOND_INTERVAL = 1;       // 毫秒精度
        static constexpr uint64_t SECOND_INTERVAL = 100;          // 100ms 精度
        static constexpr uint64_t MINUTE_INTERVAL = 1000;         // 1s 精度
        static constexpr uint64_t HOUR_INTERVAL = 1000 * 60;      // 1min 精度
        static constexpr uint64_t DAY_INTERVAL = 1000 * 60 * 60;  // 1h 精度

    private:
        TimerPrecision precision_;  // 定时器精度

        // 时间轮
        std::unique_ptr<TimerWheel> millisecond_wheel_;  // 毫秒级时间轮
        std::unique_ptr<TimerWheel> second_wheel_;       // 秒级时间轮
        std::unique_ptr<TimerWheel> minute_wheel_;       // 分钟级时间轮
        std::unique_ptr<TimerWheel> hour_wheel_;         // 小时级时间轮
        std::unique_ptr<TimerWheel> day_wheel_;          // 天级时间轮

        // 定时器节点池
        std::vector<TimerNode*> node_pool_;
        std::atomic<uint32_t> pool_index_;
        uint32_t pool_size_;

        // 空闲节点链表
        std::list<TimerNode*> free_list_;
        std::mutex free_list_mutex_;

        // 已取消的定时器链表
        std::list<TimerNode*> cancelled_list_;
        std::mutex cancelled_list_mutex_;

        // 当前时间（毫秒）
        std::atomic<uint64_t> current_time_;
        std::atomic<uint64_t> start_time_;

        // 定时器计数
        std::atomic<size_t> timer_count_;

        // 序列号计数器
        std::atomic<uint32_t> sequence_;

        // 是否已初始化
        bool initialized_;
    };

// 全局定时器管理器访问宏
#define sTimerManager cncpp::TimerManager::getMe()

    // 获取当前时间的毫秒数
    inline uint64_t GetCurrentTimeMillis()
    {
        auto now = std::chrono::steady_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

}  // namespace cncpp

#include <algorithm>
#include <iostream>
#include "timer_wheel.h"

namespace timer
{

    // ============== TimerWheel 实现 ==============

    TimerWheel::TimerWheel(const TimerWheelConfig& config)
        : config_(config),
          current_tick_(0),
          next_id_(1),
          running_(false),
          total_timers_added_(0),
          total_timers_expired_(0)
    {
        initWheels();
    }

    TimerWheel::~TimerWheel()
    {
        stop();
    }

    void TimerWheel::initWheels()
    {
        // 初始化近端轮（大小 config_.near_size）
        wheels_.resize(1 + config_.level_count);

        // near wheel
        wheels_[0].resize(config_.near_size);

        // level wheels
        for (size_t i = 1; i <= config_.level_count; ++i)
        {
            wheels_[i].resize(config_.level_size);
        }
    }

    void TimerWheel::start()
    {
        if (running_)
            return;

        running_ = true;
        worker_thread_ = std::make_unique<std::thread>(&TimerWheel::mainLoop, this);
    }

    void TimerWheel::stop()
    {
        if (!running_)
            return;

        running_ = false;
        cv_.notify_all();

        if (worker_thread_ && worker_thread_->joinable())
        {
            worker_thread_->join();
        }
    }

    TimerId TimerWheel::addTimer(uint64_t timeout_ms, TimeoutCallback callback, bool repeat)
    {
        if (timeout_ms == 0)
        {
            // 立即执行
            if (callback)
                callback();
            return 0;
        }

        uint64_t delay_ticks = (timeout_ms + config_.tick_ms - 1) / config_.tick_ms;
        uint64_t expire_ticks = current_tick_ + delay_ticks;

        auto node = std::make_unique<TimerNode>();
        TimerId id = next_id_++;
        node->id = id;
        node->callback = std::move(callback);
        node->expire_ticks = expire_ticks;
        node->repeat = repeat;
        node->interval_ticks = repeat ? delay_ticks : 0;

        // 计算放置位置
        calcPosition(expire_ticks, node->level, node->slot);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            timers_[id] = std::move(node);
            addToWheel(id, timers_[id]->level, timers_[id]->slot);
        }

        total_timers_added_++;
        cv_.notify_one();

        return id;
    }

    TimerId TimerWheel::addTimerAt(TimePoint tp, TimeoutCallback callback)
    {
        auto now = Clock::now();
        if (tp <= now)
        {
            if (callback)
                callback();
            return 0;
        }

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(tp - now);
        return addTimer(duration.count(), std::move(callback), false);
    }

    void TimerWheel::calcPosition(uint32_t ticks, size_t& level, size_t& slot)
    {
        uint32_t offset = ticks - current_tick_;

        if (offset < config_.near_size)
        {
            level = 0;
            slot = offset;
            return;
        }

        // 计算在哪个层级
        size_t step = config_.near_size;
        for (level = 1; level <= config_.level_count; ++level)
        {
            if (offset < step * config_.level_size)
            {
                slot = (offset / step) % config_.level_size;
                return;
            }
            step *= config_.level_size;
        }

        // 超出最大层级，放在最后一层最后一个槽
        level = config_.level_count;
        slot = config_.level_size - 1;
    }

    void TimerWheel::addToWheel(TimerId id, size_t level, size_t slot)
    {
        if (level == 0)
        {
            wheels_[0][slot].push_back(id);
        }
        else
        {
            wheels_[level][slot].push_back(id);
        }
    }

    void TimerWheel::removeFromWheel(TimerId id)
    {
        auto it = timers_.find(id);
        if (it == timers_.end())
            return;

        size_t level = it->second->level;
        size_t slot = it->second->slot;

        auto& wheel_slot = wheels_[level][slot];
        auto iter = std::find(wheel_slot.begin(), wheel_slot.end(), id);
        if (iter != wheel_slot.end())
        {
            wheel_slot.erase(iter);
        }
    }

    bool TimerWheel::cancelTimer(TimerId id)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = timers_.find(id);
        if (it == timers_.end())
            return false;

        removeFromWheel(id);
        timers_.erase(it);
        return true;
    }

    void TimerWheel::rescheduleTimer(TimerId id)
    {
        auto it = timers_.find(id);
        if (it == timers_.end())
            return;

        // 重新计算过期时间
        it->second->expire_ticks = current_tick_ + it->second->interval_ticks;

        // 计算新位置
        calcPosition(it->second->expire_ticks, it->second->level, it->second->slot);

        // 添加到新位置
        addToWheel(id, it->second->level, it->second->slot);
    }

    void TimerWheel::executeExpiredTimers()
    {
        std::vector<TimerId> expired_ids;
        std::vector<TimeoutCallback> callbacks;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            // 处理近端轮的当前槽位
            size_t slot = current_tick_ % config_.near_size;
            auto& near_slot = wheels_[0][slot];

            // 收集过期定时器
            for (TimerId id : near_slot)
            {
                auto it = timers_.find(id);
                if (it != timers_.end())
                {
                    expired_ids.push_back(id);
                    callbacks.push_back(it->second->callback);
                }
            }
            near_slot.clear();

            // 处理层级轮（需要降级）
            if (slot == 0)
            {
                // 检查是否需要从高层级降级定时器
                uint32_t current = current_tick_ / config_.near_size;

                for (size_t level = 1; level <= config_.level_count; ++level)
                {
                    size_t level_slot = current % config_.level_size;
                    auto& level_slot_vec = wheels_[level][level_slot];

                    if (!level_slot_vec.empty())
                    {
                        // 将定时器从高层级降级到近端轮
                        for (TimerId id : level_slot_vec)
                        {
                            auto it = timers_.find(id);
                            if (it != timers_.end())
                            {
                                // 重新计算位置（会放入近端轮）
                                removeFromWheel(id);
                                calcPosition(it->second->expire_ticks, it->second->level, it->second->slot);
                                addToWheel(id, it->second->level, it->second->slot);
                            }
                        }
                        level_slot_vec.clear();
                    }

                    if (current % config_.level_size != 0)
                        break;
                    current /= config_.level_size;
                }
            }
        }

        // 执行回调（不在锁内执行）
        for (size_t i = 0; i < expired_ids.size(); ++i)
        {
            if (callbacks[i])
            {
                callbacks[i]();
            }
            total_timers_expired_++;

            // 处理重复定时器
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = timers_.find(expired_ids[i]);
            if (it != timers_.end() && it->second->repeat)
            {
                rescheduleTimer(expired_ids[i]);
            }
            else
            {
                timers_.erase(expired_ids[i]);
            }
        }
    }

    void TimerWheel::mainLoop()
    {
        auto last_time = Clock::now();
        auto tick_duration = std::chrono::milliseconds(config_.tick_ms);

        while (running_)
        {
            auto now = Clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time);

            if (elapsed.count() >= config_.tick_ms)
            {
                current_tick_++;
                executeExpiredTimers();
                last_time = now;
            }

            // 等待下一个tick
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // ============== SimpleTimerWheel 实现 ==============

    SimpleTimerWheel::SimpleTimerWheel(size_t wheel_size, size_t tick_ms)
        : wheel_size_(wheel_size),
          tick_ms_(tick_ms),
          tick_interval_ms_(tick_ms),
          current_tick_(0),
          next_id_(1),
          running_(false)
    {
        wheel_.resize(wheel_size_);
    }

    SimpleTimerWheel::~SimpleTimerWheel()
    {
        stop();
    }

    TimerId SimpleTimerWheel::addTimer(uint64_t timeout_ms, TimeoutCallback callback, bool repeat)
    {
        if (timeout_ms == 0)
        {
            if (callback)
                callback();
            return 0;
        }

        uint64_t delay_ticks = (timeout_ms + tick_ms_ - 1) / tick_ms_;
        uint64_t expire_tick = current_tick_ + delay_ticks;

        auto entry = std::make_unique<TimerEntry>();
        TimerId id = next_id_++;
        entry->id = id;
        entry->callback = std::move(callback);
        entry->expire_tick = expire_tick;
        entry->repeat = repeat;
        entry->interval_ticks = repeat ? delay_ticks : 0;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            size_t slot = expire_tick % wheel_size_;
            wheel_[slot].push_back(id);
            timers_[id] = std::move(entry);
        }

        return id;
    }

    bool SimpleTimerWheel::cancelTimer(TimerId id)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = timers_.find(id);
        if (it == timers_.end())
            return false;

        // 从轮中移除
        size_t slot = it->second->expire_tick % wheel_size_;
        auto& slot_vec = wheel_[slot];
        auto iter = std::find(slot_vec.begin(), slot_vec.end(), id);
        if (iter != slot_vec.end())
        {
            slot_vec.erase(iter);
        }

        timers_.erase(it);
        return true;
    }

    void SimpleTimerWheel::start()
    {
        if (running_)
            return;

        running_ = true;
        worker_thread_ = std::make_unique<std::thread>(&SimpleTimerWheel::mainLoop, this);
    }

    void SimpleTimerWheel::stop()
    {
        if (!running_)
            return;

        running_ = false;
        if (worker_thread_ && worker_thread_->joinable())
        {
            worker_thread_->join();
        }
    }

    void SimpleTimerWheel::mainLoop()
    {
        while (running_)
        {
            auto start = Clock::now();

            current_tick_++;
            size_t slot = current_tick_ % wheel_size_;

            std::vector<TimerId> expired_ids;
            std::vector<TimeoutCallback> callbacks;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto& slot_vec = wheel_[slot];

                for (TimerId id : slot_vec)
                {
                    auto it = timers_.find(id);
                    if (it != timers_.end() && it->second->expire_tick <= current_tick_)
                    {
                        expired_ids.push_back(id);
                        callbacks.push_back(it->second->callback);
                    }
                }
            }

            // 执行回调
            for (size_t i = 0; i < expired_ids.size(); ++i)
            {
                if (callbacks[i])
                    callbacks[i]();

                std::lock_guard<std::mutex> lock(mutex_);
                auto it = timers_.find(expired_ids[i]);
                if (it != timers_.end())
                {
                    if (it->second->repeat)
                    {
                        // 重新调度
                        it->second->expire_tick = current_tick_ + it->second->interval_ticks;
                        size_t new_slot = it->second->expire_tick % wheel_size_;
                        wheel_[new_slot].push_back(expired_ids[i]);
                    }
                    else
                    {
                        timers_.erase(expired_ids[i]);
                    }
                }
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);

            if (elapsed.count() < tick_ms_)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(tick_ms_ - elapsed.count()));
            }
        }
    }

}  // namespace timer

#endif
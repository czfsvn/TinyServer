#include "timer_manager.h"
#include <cstring>
#include "logger.h"

#if 0
namespace cncpp
{
    // ============ TimerWheel 实现 ============

    TimerWheel::TimerWheel(int slot_count, uint64_t slot_interval)
        : slot_count_(slot_count), slot_interval_(slot_interval), slots_(slot_count)
    {
    }

    TimerWheel::~TimerWheel()
    {
        // 清理所有槽位
        for (auto& slot : slots_)
        {
            for (auto* node : slot)
            {
                if (node && !node->cancelled_)
                {
                    delete node;
                }
            }
        }
    }

    void TimerWheel::AddTimer(TimerNode* node)
    {
        if (!node)
        {
            return;
        }

        // 计算定时器应该放在哪个槽位
        int64_t diff = static_cast<int64_t>(node->expire_) - static_cast<int64_t>(TimerManager::getMe().GetNow());
        if (diff <= 0)
        {
            diff = 1;
        }

        // 计算需要跨越的槽位数
        int slots_to_skip = static_cast<int>(diff / slot_interval_);
        if (slots_to_skip >= slot_count_)
        {
            slots_to_skip = slot_count_ - 1;
        }

        int target_slot = (slots_.size() + slots_to_skip) % slots_.size();
        slots_[target_slot].push_back(node);
    }

    void TimerWheel::Trigger(int current_slot)
    {
        if (current_slot < 0 || current_slot >= slot_count_)
        {
            return;
        }

        auto& slot = slots_[current_slot];
        for (auto* node : slot)
        {
            if (node && !node->cancelled_)
            {
                // 调用回调函数
                if (node->callback_)
                {
                    try
                    {
                        node->callback_();
                    } catch (const std::exception& e)
                    {
                        LOG_ERROR("Timer callback exception: {}", e.what());
                    } catch (...)
                    {
                        LOG_ERROR("Timer callback unknown exception");
                    }
                }
            }
        }
        slot.clear();
    }

    // ============ TimerManager 实现 ============

    TimerManager::TimerManager()
        : precision_(TimerPrecision::MILLISECOND),
          pool_index_(0),
          pool_size_(0),
          current_time_(0),
          start_time_(0),
          timer_count_(0),
          sequence_(0),
          initialized_(false)
    {
    }

    TimerManager::~TimerManager()
    {
        Clear();
    }

    bool TimerManager::Initialize(uint32_t pool_size)
    {
        if (initialized_)
        {
            LOG_WARN("TimerManager already initialized");
            return true;
        }

        try
        {
            pool_size_ = pool_size;
            node_pool_.resize(pool_size);
            pool_index_.store(0);

            // 预分配定时器节点
            for (uint32_t i = 0; i < pool_size; ++i)
            {
                node_pool_[i] = new TimerNode();
                free_list_.push_back(node_pool_[i]);
            }

            // 创建时间轮
            millisecond_wheel_ = std::make_unique<TimerWheel>(MILLISECOND_SLOTS, MILLISECOND_INTERVAL);
            second_wheel_ = std::make_unique<TimerWheel>(SECOND_SLOTS, SECOND_INTERVAL);
            minute_wheel_ = std::make_unique<TimerWheel>(MINUTE_SLOTS, MINUTE_INTERVAL);
            hour_wheel_ = std::make_unique<TimerWheel>(HOUR_SLOTS, HOUR_INTERVAL);
            day_wheel_ = std::make_unique<TimerWheel>(DAY_SLOTS, DAY_INTERVAL);

            // 记录启动时间
            start_time_.store(GetCurrentTimeMillis());
            current_time_.store(start_time_.load());

            initialized_ = true;
            LOG_INFO("TimerManager initialized with pool size {}", pool_size);
            return true;
        } catch (const std::exception& e)
        {
            LOG_ERROR("Failed to initialize TimerManager: {}", e.what());
            return false;
        }
    }

    TimerNode* TimerManager::AddTimer(uint64_t delay_ms, std::function<void()> callback, TimerMode mode)
    {
        if (!initialized_)
        {
            LOG_ERROR("TimerManager not initialized");
            return nullptr;
        }

        if (!callback)
        {
            LOG_ERROR("Timer callback is null");
            return nullptr;
        }

        // 分配定时器节点
        TimerNode* node = AllocNode();
        if (!node)
        {
            LOG_ERROR("Failed to allocate timer node");
            return nullptr;
        }

        // 设置定时器节点
        node->expire_ = current_time_.load() + delay_ms;
        node->interval_ = delay_ms;
        node->mode_ = mode;
        node->cancelled_ = false;
        node->callback_ = std::move(callback);
        node->sequence_ = sequence_.fetch_add(1);

        // 添加到时间轮
        AddToWheel(node);
        timer_count_.fetch_add(1);

        return node;
    }

    TimerNode* TimerManager::AddRepeatTimer(uint64_t interval_ms, std::function<void()> callback)
    {
        return AddTimer(interval_ms, std::move(callback), TimerMode::REPEAT);
    }

    void TimerManager::RemoveTimer(TimerNode* node)
    {
        if (!node)
        {
            return;
        }

        if (node->cancelled_.exchange(true))
        {
            // 已经取消过了
            return;
        }

        // 添加到已取消列表
        std::lock_guard<std::mutex> lock(cancelled_list_mutex_);
        cancelled_list_.push_back(node);
        timer_count_.fetch_sub(1);
    }

    void TimerManager::Update()
    {
        if (!initialized_)
        {
            return;
        }

        // 更新当前时间
        uint64_t now = GetCurrentTimeMillis();
        current_time_.store(now);

        // 计算需要触发哪些槽位
        uint64_t elapsed = now - start_time_.load();

        // 触发毫秒级时间轮
        int ms_slot = (elapsed / MILLISECOND_INTERVAL) % MILLISECOND_SLOTS;
        millisecond_wheel_->Trigger(ms_slot);

        // 触发秒级时间轮（每100ms触发一次）
        if ((elapsed % SECOND_INTERVAL) == 0)
        {
            int sec_slot = (elapsed / SECOND_INTERVAL) % SECOND_SLOTS;
            second_wheel_->Trigger(sec_slot);
        }

        // 触发分钟级时间轮（每秒触发一次）
        if ((elapsed % MINUTE_INTERVAL) == 0)
        {
            int min_slot = (elapsed / MINUTE_INTERVAL) % MINUTE_SLOTS;
            minute_wheel_->Trigger(min_slot);
        }

        // 触发小时级时间轮（每分钟触发一次）
        if ((elapsed % HOUR_INTERVAL) == 0)
        {
            int hour_slot = (elapsed / HOUR_INTERVAL) % HOUR_SLOTS;
            hour_wheel_->Trigger(hour_slot);
        }

        // 触发天级时间轮（每小时触发一次）
        if ((elapsed % DAY_INTERVAL) == 0)
        {
            int day_slot = (elapsed / DAY_INTERVAL) % DAY_SLOTS;
            day_wheel_->Trigger(day_slot);
        }

        // 清理已取消的定时器
        CleanupCancelled();
    }

    uint64_t TimerManager::GetNow() const
    {
        return current_time_.load();
    }

    uint64_t TimerManager::GetElapsed() const
    {
        return GetCurrentTimeMillis() - start_time_.load();
    }

    void TimerManager::Clear()
    {
        if (!initialized_)
        {
            return;
        }

        // 清理时间轮
        millisecond_wheel_.reset();
        second_wheel_.reset();
        minute_wheel_.reset();
        hour_wheel_.reset();
        day_wheel_.reset();

        // 清理节点池
        for (auto* node : node_pool_)
        {
            delete node;
        }
        node_pool_.clear();
        free_list_.clear();

        // 清理已取消列表
        {
            std::lock_guard<std::mutex> lock(cancelled_list_mutex_);
            cancelled_list_.clear();
        }

        pool_size_ = 0;
        pool_index_.store(0);
        timer_count_.store(0);
        initialized_ = false;

        LOG_INFO("TimerManager cleared");
    }

    TimerNode* TimerManager::AllocNode()
    {
        std::lock_guard<std::mutex> lock(free_list_mutex_);

        if (free_list_.empty())
        {
            // 扩容
            uint32_t new_size = pool_size_ * 2;
            if (new_size == 0)
            {
                new_size = 1024;
            }

            std::vector<TimerNode*> new_pool;
            new_pool.resize(new_size);

            for (uint32_t i = pool_size_; i < new_size; ++i)
            {
                new_pool[i] = new TimerNode();
                free_list_.push_back(new_pool[i]);
            }

            node_pool_ = std::move(new_pool);
            pool_size_ = new_size;

            LOG_INFO("Timer node pool expanded to {}", new_size);
        }

        auto* node = free_list_.front();
        free_list_.pop_front();
        return node;
    }

    void TimerManager::FreeNode(TimerNode* node)
    {
        if (!node)
        {
            return;
        }

        // 重置节点状态
        node->expire_ = 0;
        node->interval_ = 0;
        node->sequence_ = 0;
        node->mode_ = TimerMode::ONCE;
        node->cancelled_ = false;
        node->callback_ = nullptr;
        node->next_ = nullptr;

        // 添加到空闲列表
        std::lock_guard<std::mutex> lock(free_list_mutex_);
        free_list_.push_back(node);
    }

    void TimerManager::AddToWheel(TimerNode* node)
    {
        if (!node)
        {
            return;
        }

        uint64_t now = current_time_.load();
        uint64_t diff = node->expire_ - now;

        // 根据延迟时间选择合适的时间轮
        if (diff < SECOND_INTERVAL)
        {
            // 毫秒级：延迟 < 100ms
            millisecond_wheel_->AddTimer(node);
        }
        else if (diff < MINUTE_INTERVAL)
        {
            // 秒级：延迟 < 1s
            second_wheel_->AddTimer(node);
        }
        else if (diff < HOUR_INTERVAL)
        {
            // 分钟级：延迟 < 1min
            minute_wheel_->AddTimer(node);
        }
        else if (diff < DAY_INTERVAL)
        {
            // 小时级：延迟 < 1h
            hour_wheel_->AddTimer(node);
        }
        else
        {
            // 天级：延迟 >= 1h
            day_wheel_->AddTimer(node);
        }
    }

    void TimerManager::CleanupCancelled()
    {
        std::lock_guard<std::mutex> lock(cancelled_list_mutex_);

        for (auto* node : cancelled_list_)
        {
            FreeNode(node);
        }
        cancelled_list_.clear();
    }

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
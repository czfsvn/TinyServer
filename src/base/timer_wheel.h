#pragma once
#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <vector>
#include "singleton.h"

namespace cncpp
{
    class Timer;
    class TimerWheel;
    using TimerPtr      = std::shared_ptr<Timer>;
    using TimerWheelPtr = std::shared_ptr<TimerWheel>;

    using TimerCallBack = std::function<void()>;

    void main();

    enum class TimerType
    {
        None      = 0,
        Repeated  = 1,  // 多次
        Infinited = 2,  // 无限次数
        max,
    };

    class Timer
    {
    public:
        Timer(const uint64_t timerid, const uint64_t expire, const TimerCallBack& cb,
              const TimerType type = TimerType::None);
        virtual ~Timer() = default;

        void        run();
        std::string toString() const;

        TimerType getType() const;

        uint64_t getTimerId() const;

        void     setInterval(const uint64_t interval);
        uint64_t getInterval() const;

        void     setRunCount(const uint32_t runcount);
        uint32_t getRunCount() const;

        void     setExpireMilliSec(const uint64_t expire);
        uint64_t getExpireMilliSec() const;

        uint64_t getCreateTime() const;

    private:
        uint64_t      timer_id_          = 0;                // 定时器ID
        uint64_t      create_milli_time_ = 0;                // 定时器创建时间(毫秒)
        uint64_t      interval_          = 0;                // 执行间隔时间(毫秒)
        uint32_t      runcount_          = 0;                // 总共执行次数
        uint64_t      expire_            = 0;                // 执行时间点(毫秒)
        TimerType     type_              = TimerType::None;  // 定时器类型
        TimerCallBack callback_          = nullptr;
    };

    class TimerWheel
    {
    public:
        TimerWheel(const uint32_t slotsize, const uint16_t level);

        void addTimer(TimerPtr timer);
        void removeTimer(TimerPtr timer);

        uint16_t getLevel() const;
        uint32_t getCurSlot() const;
        uint32_t getSlotSize() const;

        uint64_t getMaxTick() const;
        uint64_t getMilliPerTick() const;
        uint64_t getTimeRange() const;

        void initSlots();

        void tick();
        void shift();
        void runTimers();

    private:
        uint32_t cur_slot_  = 0;  // 初始化的时候，均从第一个槽开始
        uint32_t slot_size_ = 0;  // 轮槽的格数
        uint16_t level_     = 0;  // 级别

        bool                             trigger_shift_  = false;  // 是否触发时间轮进位
        uint64_t                         max_tick_       = 0;      // 最大时间轮刻度
        uint64_t                         milli_per_tick_ = 0;      // 每个刻度的时间间隔(毫秒)
        uint64_t                         time_range_     = 0;      // 时间轮的时间范围(毫秒)
        std::vector<std::list<TimerPtr>> timers_         = {};
    };

    class TimerManager : public cncpp::Singleton<TimerManager>
    {
    public:
        TimerManager();
        ~TimerManager();

        void init();

        uint64_t generateTimerIdx();
        uint64_t getCurMilliSecond() const;

        void addTimer(TimerPtr timer);
        void removeTimer(TimerPtr timer);
        //
        void runAt(const uint64_t expire, TimerCallBack cb);
        void runAfter(const uint64_t expire, TimerCallBack cb);
        void runEvery(const uint64_t interval, TimerCallBack cb, const uint32_t runcount);
        void runForever(const uint64_t interval, TimerCallBack cb);

        void shift(const uint16_t nextlevel);
        void tick();

        // 添加停止方法
        void stop();

        void addTempTimer(TimerPtr timer);
        void addTempTimer(std::list<TimerPtr>& timerlist);
        void reAddTimer();

        void removeTempTimer(TimerPtr timer);

        void print();

        TimerWheelPtr getTimerWheel(const uint16_t level);

    private:
    private:
        std::map<uint16_t, TimerWheelPtr> wheels_map_  = {};
        std::list<TimerPtr>               temp_timers_ = {};

        // 超长期定时器(一时半会不会得到执行)
        std::list<TimerPtr> longtime_timers_ = {};

        uint64_t timer_idx_         = 0;
        uint64_t cur_milli_seconds_ = 0;

        // 停止标志
        std::atomic<bool> running_{false};
    };
}  // namespace cncpp

#define sTimerManager cncpp::TimerManager::getMe()

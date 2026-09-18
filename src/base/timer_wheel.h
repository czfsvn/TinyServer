#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>
#include "singleton.h"

namespace cncpp
{
    class Timer;
    class TimerWheel;
    using TimerPtr      = std::shared_ptr<Timer>;
    using TimerWheelPtr = std::shared_ptr<TimerWheel>;

    using TimerCallBack = std::function<void()>;

    // 非法定时器 id, 注册失败时返回; 正常 id 从 1 开始
    constexpr uint64_t INVALID_TIMER_ID = 0;

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
        // tick_ms: 一个刻度代表的毫秒数, 由驱动方(主循环周期)决定, 见 ADR-0001
        TimerWheel(const uint32_t slotsize, const uint16_t level, const uint64_t tick_ms);

        // total_ticks: 已经走过的基础刻度总数(即 level-1 的 tick 次数), 用于修正"当前格已经走了一半"的偏差
        void addTimer(TimerPtr timer, const uint64_t total_ticks);

        uint16_t getLevel() const;
        uint32_t getCurSlot() const;
        uint32_t getSlotSize() const;

        // 一个刻度的毫秒数
        uint64_t getTickMs() const;
        uint64_t getMaxTick() const;
        uint64_t getMilliPerTick() const;
        uint64_t getTimeRange() const;

        void initSlots();

        void tick();
        void shift();

    private:
        uint32_t cur_slot_  = 0;  // 初始化的时候，均从第一个槽开始
        uint32_t slot_size_ = 0;  // 轮槽的格数
        uint16_t level_     = 0;  // 级别
        uint64_t tick_ms_   = 0;  // 一个刻度的毫秒数

        bool                             trigger_shift_   = false;  // 是否触发时间轮进位
        uint64_t                         max_tick_        = 0;      // 最大时间轮刻度
        uint64_t                         milli_per_tick_  = 0;      // 每个刻度的时间间隔(毫秒)
        uint64_t                         time_range_      = 0;      // 时间轮的时间范围(毫秒)
        std::vector<std::list<TimerPtr>> timers_         = {};
    };

    class TimerManager : public cncpp::Singleton<TimerManager>
    {
    public:
        TimerManager();
        ~TimerManager();

        // tick_ms: 一个刻度代表的毫秒数, 传 0 则回退到 DEFAULT_TICK_MS
        void init(const uint64_t tick_ms);

        uint64_t generateTimerIdx();

        // 以下注册接口可在任意线程调用: 它们只把操作投进入站队列, 真正的入轮由 tick 线程完成(见 ADR-0002)
        // 返回定时器 id 用于 cancel(); 注册到生效最多延迟一拍, 但到期时间本身按绝对时刻计算, 不损失精度
        uint64_t runAt(const uint64_t expire, TimerCallBack cb);
        uint64_t runAfter(const uint64_t delay, TimerCallBack cb);
        uint64_t runEvery(const uint64_t interval, TimerCallBack cb, const uint32_t runcount);
        uint64_t runForever(const uint64_t interval, TimerCallBack cb);

        // 惰性取消(见 ADR-0003): 只投递请求, 真正的拦截发生在 tick 线程
        bool cancel(const uint64_t timer_id);

        void shift(const uint16_t nextlevel);
        void tick();

        // 添加停止方法
        void stop();

        void addTempTimer(TimerPtr timer);
        void addTempTimer(std::list<TimerPtr>& timerlist);

        void print();

        TimerWheelPtr getTimerWheel(const uint16_t level);

        // 查询/清理取消标记, 仅供 TimerWheel::tick() 在 tick 线程调用
        bool isCancelled(const uint64_t timer_id) const;
        void eraseCancelled(const uint64_t timer_id);

    private:
        // 入站操作: 注册与取消都走这条队列, 由 tick 线程在 applyPendingOps() 里消费
        struct PendingOp
        {
            bool     is_cancel = false;
            TimerPtr timer     = nullptr;  // is_cancel == false 时有效
            uint64_t timer_id  = 0;        // is_cancel == true 时有效
        };

        // 仅 tick 线程调用: 真正放入时间轮
        void addTimer(TimerPtr timer);

        void pushPendingOp(PendingOp op);
        void applyPendingOps();

    private:
        std::map<uint16_t, TimerWheelPtr> wheels_map_  = {};
        std::list<TimerPtr>               temp_timers_ = {};

        // 超长期定时器(一时半会不会得到执行)
        std::list<TimerPtr> longtime_timers_ = {};

        uint64_t timer_idx_ = 0;

        // 已走过的基础刻度总数(level-1 的 tick 次数), 恒等于 wheels_map_[1]->getCurSlot() 所在的圈数累计
        uint64_t total_ticks_ = 0;

        // 入站队列: 整个 TimerManager 里唯一需要加锁的数据结构, 双缓冲 swap 把临界区压到最短
        std::mutex             pending_mutex_;
        std::vector<PendingOp> pending_ops_ = {};

        // 已取消、但尚未走到其槽位的定时器 id, 仅 tick 线程访问
        std::unordered_set<uint64_t> cancelled_ = {};

        // 停止标志
        std::atomic<bool> running_{false};
    };
}  // namespace cncpp

#define sTimerManager cncpp::TimerManager::getMe()

#include "timer_wheel.h"
#include "Misc.h"
#include "TimeUtils.h"
#include "logger.h"
#include "stringutil.h"

namespace cncpp
{
    static const uint16_t MAX_TIMER_LEVEL    = 5;
    static const uint16_t TIMER_SLOT_SIZE    = 10;
    static const uint64_t MILLI_SEC_PER_TICK = 10;

    /*
    uint64_t getNowMilliSecond()
    {
        return cncpp::getNowMilliSecond();
        // return TimerManager::getMe().getCurMilliSecond();
    }
    */

    Timer::Timer(const uint64_t timerid, const uint64_t expire, const TimerCallBack& cb, const TimerType type)
        : timer_id_(timerid),
          create_milli_time_(cncpp::getNowMilliSecond()),
          expire_(expire),
          callback_(cb),
          type_(type)
    {
    }

    void Timer::run()
    {
        if (!callback_)
            return;

        callback_();

        LOG_TRACE("[Timer][run] {}", toString());
    }

    std::string Timer::toString() const
    {
        return cncpp::format("timerid:{}, now={}, expire={}, interval={}, count={}", timer_id_,
                             cncpp::getNowMilliSecond() % 10000, expire_ % 10000, interval_, runcount_);
    }

    TimerType Timer::getType() const
    {
        return type_;
    }

    uint64_t Timer::getTimerId() const
    {
        return timer_id_;
    }

    void Timer::setInterval(const uint64_t interval)
    {
        interval_ = interval;
    }

    void Timer::setRunCount(const uint32_t runcount)
    {
        runcount_ = runcount;
    }

    void Timer::setExpireMilliSec(const uint64_t expire)
    {
        expire_ = expire;
    }

    uint64_t Timer::getCreateTime() const
    {
        return create_milli_time_;
    }

    uint64_t Timer::getExpireMilliSec() const
    {
        return expire_;
    }

    uint32_t Timer::getRunCount() const
    {
        return runcount_;
    }

    uint64_t Timer::getInterval() const
    {
        return interval_;
    }

    TimerWheel::TimerWheel(const uint32_t slotsize, const uint16_t level) : slot_size_(slotsize), level_(level)
    {
        initSlots();
    }
    void TimerWheel::initSlots()
    {
        timers_.clear();
        timers_.resize(slot_size_);
        max_tick_       = std::pow(slot_size_, level_);
        milli_per_tick_ = MILLI_SEC_PER_TICK * std::pow(slot_size_, SAFE_SUB(level_, 1));
        time_range_     = MILLI_SEC_PER_TICK * std::pow(slot_size_, level_);
        LOG_TRACE("[TimerWheel][initSlots] level={}, slot_size={}, max_tick={}, milli_per_tick={}, time_range={}",
                  level_, slot_size_, max_tick_, milli_per_tick_, time_range_);
    }

    uint16_t TimerWheel::getLevel() const
    {
        return level_;
    }

    uint32_t TimerWheel::getCurSlot() const
    {
        return cur_slot_;
    }

    uint32_t TimerWheel::getSlotSize() const
    {
        return slot_size_;
    }

    // 获取当前时间轮的最大tick数
    uint64_t TimerWheel::getMaxTick() const
    {
        return std::pow(slot_size_, level_);
    }

    // 获取每一个tick实际的毫秒数
    uint64_t TimerWheel::getMilliPerTick() const
    {
        return MILLI_SEC_PER_TICK * std::pow(slot_size_, SAFE_SUB(level_, 1));
    }

    // 获取当前时间轮的时间范围
    uint64_t TimerWheel::getTimeRange() const
    {
        return MILLI_SEC_PER_TICK * std::pow(slot_size_, level_);
    }

    void TimerWheel::addTimer(TimerPtr timer)
    {
        if (!timer)
            return;

        const uint32_t expire    = SAFE_SUB(timer->getExpireMilliSec(), getNowMilliSecond());
        const uint64_t tick_time = getMilliPerTick();
        if (!tick_time)
            return;

        uint32_t nextslot = 0;
        if (expire < tick_time)
        {
            nextslot = 0;
        }
        else
        {
            nextslot = expire / tick_time;
            nextslot += (expire % tick_time > 0 ? 1 : 0);
        }

        uint32_t slot = SAFE_SUB(cur_slot_ + nextslot, 1) % slot_size_;
        timers_[slot].push_back(timer);

        LOG_DEBUG(
            "[TimerWheel][addTimer] level={}, slot={}, curslot={}, nextslot={}, expire={}, "
            "tick_time={}, timer: {}",
            level_, slot, cur_slot_, nextslot, expire, tick_time, timer->toString());
    }

    void TimerWheel::tick()
    {
        std::list<TimerPtr>& timer_list = timers_[cur_slot_];
        for (auto& timer : timer_list)
        {
            if (!timer)
                continue;

            if (timer->getType() == TimerType::None)
                continue;

            timer->run();
            if (timer->getType() == TimerType::Repeated)
            {
                // 只剩下一次或者只执行一次的timer
                if (timer->getRunCount() <= 1)
                    continue;

                timer->setRunCount(SAFE_SUB(timer->getRunCount(), 1));
            }

            timer->setExpireMilliSec(getNowMilliSecond() + timer->getInterval());
            TimerManager::getMe().addTempTimer(timer);
        }

        timer_list.clear();
        ++cur_slot_;
        cur_slot_ = cur_slot_ % slot_size_;
        if (cur_slot_ == 0)
        {
            LOG_TRACE("[TimerWheel][shift] level={}, curslot={}, prelevel={}, preslot={}", level_, cur_slot_,
                      SAFE_SUB(level_, 1), getCurSlot());
            sTimerManager.shift(level_ + 1);
        }
    }

    void TimerWheel::shift()
    {
        std::list<TimerPtr>& timer_list = timers_[cur_slot_];
        TimerManager::getMe().addTempTimer(timer_list);

        timer_list.clear();
        ++cur_slot_;
        cur_slot_ = cur_slot_ % slot_size_;
        if (cur_slot_ == 0)
        {
            LOG_INFO("[TimerWheel][shift] level={}, curslot={}, prelevel={}, preslot={}", level_, cur_slot_,
                     SAFE_SUB(level_, 1), getCurSlot());
            sTimerManager.shift(level_ + 1);
        }
    }

    void removeTimer(TimerPtr timer)
    {
        (void)timer;
    }

    TimerManager::TimerManager()
    {
    }

    TimerManager::~TimerManager()
    {
    }

    void TimerManager::init()
    {
        wheels_map_.clear();
        cur_milli_seconds_ = cncpp::getNowMilliSecond();
        for (uint16_t i = 1; i <= MAX_TIMER_LEVEL; i++)
        {
            wheels_map_.emplace(i, std::make_shared<TimerWheel>(TIMER_SLOT_SIZE, i));
        }
    }

    uint64_t TimerManager::generateTimerIdx()
    {
        return ++timer_idx_;
    }

    uint64_t TimerManager::getCurMilliSecond() const
    {
        return cur_milli_seconds_;
    }

    void TimerManager::runAt(uint64_t expire, TimerCallBack cb)
    {
        if (!cb)
            return;

        if (expire == 0)
            expire = getNowMilliSecond();

        TimerPtr timer = std::make_shared<Timer>(generateTimerIdx(), expire, cb, TimerType::Repeated);
        if (!timer)
            return;

        timer->setRunCount(1);
        timer->setInterval(0);

        addTimer(timer);
    }

    void TimerManager::runAfter(const uint64_t expire, TimerCallBack cb)
    {
        runAt(getNowMilliSecond() + expire, cb);
    }

    void TimerManager::runEvery(uint64_t interval, TimerCallBack cb, const uint32_t runcount)
    {
        if (!cb || !interval || !runcount)
            return;

        TimerPtr timer
            = std::make_shared<Timer>(generateTimerIdx(), getNowMilliSecond() + interval, cb, TimerType::Repeated);
        if (!timer)
            return;

        timer->setRunCount(runcount);
        timer->setInterval(interval);

        addTimer(timer);
    }

    void TimerManager::runForever(uint64_t interval, TimerCallBack cb)
    {
        if (!cb || !interval)
            return;

        TimerPtr timer
            = std::make_shared<Timer>(generateTimerIdx(), getNowMilliSecond() + interval, cb, TimerType::Infinited);
        if (!timer)
            return;

        timer->setRunCount(0);
        timer->setInterval(interval);

        addTimer(timer);
    }

    void TimerManager::addTimer(TimerPtr timer)
    {
        if (!timer)
            return;

        bool           is_add = false;
        const uint32_t expire = SAFE_SUB(timer->getExpireMilliSec(), getNowMilliSecond());
        for (auto& item : wheels_map_)
        {
            TimerWheelPtr wheel = item.second;
            if (!wheel)
                continue;

            if (expire >= wheel->getTimeRange())
                continue;

            wheel->addTimer(timer);
            is_add = true;
            break;
        }

        if (!is_add)
        {
            // 放入超长定时去里列表
            longtime_timers_.emplace_back(timer);
            LOG_ERROR("[TimerManager][addTimer] timer expire is too long, expire:{}", expire);
        }
    }

    TimerWheelPtr TimerManager::getTimerWheel(const uint16_t level)
    {
        auto iter = wheels_map_.find(level);
        if (iter == wheels_map_.end())
            return nullptr;

        return iter->second;
    }

    void TimerManager::removeTimer(TimerPtr timer)
    {
        (void)timer;
    }

    void TimerManager::shift(const uint16_t level)
    {
        TimerWheelPtr wheel = getTimerWheel(level);
        if (!wheel)
        {
            // 达到最高级了，需要将长效定时器重新加入到slot
            addTempTimer(longtime_timers_);
            longtime_timers_.clear();
            return;
        }

        wheel->shift();
    }

    void TimerManager::timer_update()
    {
        LOG_TRACE("[TimerManager][timer_update]");
        TimerWheelPtr wheel = getTimerWheel(1);
        if (wheel)
        {
            wheel->tick();
        }

        for (auto& item : temp_timers_)
        {
            if (!item)
                continue;

            addTimer(item);
        }
        temp_timers_.clear();
    }

    void TimerManager::addTempTimer(TimerPtr timer)
    {
        if (!timer)
            return;

        temp_timers_.emplace_back(timer);
    }

    void TimerManager::addTempTimer(std::list<TimerPtr>& timerlist)
    {
        if (timerlist.empty())
            return;

        temp_timers_.splice(temp_timers_.end(), timerlist);
    }

    void TimerManager::timer_run()
    {
        running_ = true;
        while (running_)
        {
            cncpp::sleepfor_milliseconds(MILLI_SEC_PER_TICK / 5);
            timer_update();
        }
        LOG_INFO("[TimerManager][timer_run] stopped");
    }

    void TimerManager::stop()
    {
        running_ = false;
        LOG_INFO("[TimerManager][stop] stopping timer manager");
    }

    void TimerManager::print()
    {
        for (auto& item : wheels_map_)
        {
            TimerWheelPtr wheel = item.second;
            if (!wheel)
                continue;

            LOG_INFO(
                "[TimerManager][print] level:{}, slot_size:{}, cur_slot:{}, max_tick:{}, "
                "\ttimerange={}",
                wheel->getLevel(), wheel->getSlotSize(), wheel->getCurSlot(), wheel->getMaxTick(),
                wheel->getTimeRange());
        }
    }
}  // namespace cncpp
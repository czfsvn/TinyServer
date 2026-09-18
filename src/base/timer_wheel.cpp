#include "timer_wheel.h"
#include "Misc.h"
#include "TimeUtils.h"
#include "logger.h"
#include "stringutil.h"

#include <exception>
#include <utility>

namespace cncpp
{
    static const uint16_t MAX_TIMER_LEVEL = 5;
    static const uint16_t TIMER_SLOT_SIZE = 10;
    // 驱动方没有给出有效周期时的兜底值, 见 ADR-0001
    static const uint64_t DEFAULT_TICK_MS = 50;

    namespace
    {
        // 整数快速幂, 取代 std::pow(slot_size_, level_): 后者返回 double, 赋给 uint64_t 会有精度隐患
        uint64_t intPow(const uint64_t base, const uint16_t exp)
        {
            uint64_t result = 1;
            for (uint16_t i = 0; i < exp; ++i)
                result *= base;
            return result;
        }

        // 第 level 级时间轮的上一级指数, level 最小为 1
        uint16_t prevLevel(const uint16_t level)
        {
            return static_cast<uint16_t>(SAFE_SUB(level, 1));
        }
    }  // namespace

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

        // 回调抛异常不能穿透 tick(), 否则会打穿主循环
        try
        {
            callback_();
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("[Timer][run] timerid={} callback threw exception: {}", timer_id_, e.what());
        }
        catch (...)
        {
            LOG_ERROR("[Timer][run] timerid={} callback threw unknown exception", timer_id_);
        }

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

    TimerWheel::TimerWheel(const uint32_t slotsize, const uint16_t level, const uint64_t tick_ms)
        : slot_size_(slotsize), level_(level), tick_ms_(tick_ms)
    {
        initSlots();
    }
    void TimerWheel::initSlots()
    {
        timers_.clear();
        timers_.resize(slot_size_);
        max_tick_       = intPow(slot_size_, level_);
        milli_per_tick_ = tick_ms_ * intPow(slot_size_, prevLevel(level_));
        time_range_     = tick_ms_ * intPow(slot_size_, level_);

        LOG_TRACE("[TimerWheel][initSlots] level={}, slot_size={}, tick_ms={}, max_tick={}, milli_per_tick={}, "
                  "time_range={}",
                  level_, slot_size_, tick_ms_, max_tick_, milli_per_tick_, time_range_);
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

    // 一个刻度的毫秒数
    uint64_t TimerWheel::getTickMs() const
    {
        return tick_ms_;
    }

    // 获取当前时间轮的最大tick数
    uint64_t TimerWheel::getMaxTick() const
    {
        return max_tick_;
    }

    // 获取每一个tick实际的毫秒数
    uint64_t TimerWheel::getMilliPerTick() const
    {
        return milli_per_tick_;
    }

    // 获取当前时间轮的时间范围
    uint64_t TimerWheel::getTimeRange() const
    {
        return time_range_;
    }

    void TimerWheel::addTimer(TimerPtr timer, const uint64_t total_ticks)
    {
        if (!timer)
            return;

        // 用 uint64_t: 原来是 SAFE_SUB(uint64, uint64) 截断成 uint32_t, >49 天就溢出
        const uint64_t now_ms    = getNowMilliSecond();
        const uint64_t expire_ms = timer->getExpireMilliSec() > now_ms ? timer->getExpireMilliSec() - now_ms : 0;
        if (!milli_per_tick_)
            return;

        // 本级轮每 period_ticks 个基础刻度才走一格(max_tick_ / slot_size_ = slot_size_^(level-1)).
        // "现在"通常已经走在某一格的中间, 所以放在下一格上并不等于"再过 milli_per_tick_ 毫秒",
        // 而是要减掉这一格里已经走过的 rem_ms. 不减的话高层轮最多会提前一个本级刻度触发.
        const uint64_t period_ticks = slot_size_ > 0 ? max_tick_ / slot_size_ : 1;
        const uint64_t rem_ms       = period_ticks > 0 ? (total_ticks % period_ticks) * tick_ms_ : 0;

        // 把定时器放在第 nextslot 拍上, 它会在下面这个时刻被"处理":
        //   at(nextslot) = nextslot * milli_per_tick_ - rem_ms
        uint64_t nextslot      = (expire_ms + rem_ms) / milli_per_tick_;
        const uint64_t in_slot = (expire_ms + rem_ms) % milli_per_tick_;

        // level 1 的槽是直接执行的, 必须取"不早于到期时间"的那一拍 -> 向上取整
        // level >= 2 的槽只做降级(shift() 把定时器丢回 temp_timers_ 重新入轮), 必须取"不晚于到期时间"的
        // 那一拍 -> 向下取整, 零头留给低一级的轮精确摆放.
        // 这里搞反的话, 高级轮的定时器会晚整整一个本级刻度才触发(level2 就是 500ms)
        if (level_ == 1 && in_slot > 0)
            ++nextslot;

        if (nextslot < 1)
            nextslot = 1;
        // 一个时间轮最多只能表示 slot_size_ 拍, 钳住上界
        if (nextslot > slot_size_)
            nextslot = slot_size_;

        // tick() 先跑 timers_[cur_slot_] 再 ++cur_slot_, 也就是说两次驱动之间 cur_slot_ 指向的是"下一拍要跑的槽",
        // 落在 cur_slot_ 上就等于"下一拍执行", 所以是 + nextslot - 1 而不是 + nextslot.
        // 这里必须是纯模运算: SAFE_SUB 的钳位会让 cur_slot_ == 0 时落到当前槽, 同一份逻辑在
        // cur_slot_ 为 0 和非 0 时行为不一致.
        const uint32_t slot = static_cast<uint32_t>((cur_slot_ + nextslot - 1) % slot_size_);
        timers_[slot].push_back(timer);

        LOG_DEBUG(
            "[TimerWheel][addTimer] level={}, slot={}, curslot={}, nextslot={}, expire={}, rem={}, "
            "milli_per_tick={}, timer: {}",
            level_, slot, cur_slot_, nextslot, expire_ms, rem_ms, milli_per_tick_, timer->toString());
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

            // 惰性取消: 命中就直接跳过, 既不执行也不重新入轮(见 ADR-0003)
            if (TimerManager::getMe().isCancelled(timer->getTimerId()))
            {
                TimerManager::getMe().eraseCancelled(timer->getTimerId());
                continue;
            }

            timer->run();
            if (timer->getType() == TimerType::Repeated)
            {
                // 只剩下一次或者只执行一次的timer
                if (timer->getRunCount() <= 1)
                    continue;

                timer->setRunCount(SAFE_SUB(timer->getRunCount(), 1));
            }

            // 基于上一次的到期时间累加, 而不是"现在 + 间隔": 后者会把回调耗时累积进去, 长期漂移
            timer->setExpireMilliSec(timer->getExpireMilliSec() + timer->getInterval());
            TimerManager::getMe().addTempTimer(timer);
        }

        timer_list.clear();
        ++cur_slot_;
        cur_slot_ = cur_slot_ % slot_size_;
        if (cur_slot_ == 0)
        {
            //LOG_TRACE("[TimerWheel][shift] level={}, curslot={}, prelevel={}, preslot={}", level_, cur_slot_,
            //          SAFE_SUB(level_, 1), getCurSlot());
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
            //LOG_TRACE("[TimerWheel][shift] level={}, curslot={}, prelevel={}, preslot={}", level_, cur_slot_,
            //          SAFE_SUB(level_, 1), getCurSlot());
            sTimerManager.shift(level_ + 1);
        }
    }

    TimerManager::TimerManager()
    {
    }

    TimerManager::~TimerManager()
    {
    }

    void TimerManager::init(const uint64_t tick_ms)
    {
        uint64_t real_tick_ms = tick_ms;
        if (real_tick_ms == 0)
        {
            real_tick_ms = DEFAULT_TICK_MS;
            LOG_WARN("[TimerManager][init] tick_ms is 0, fallback to {} ms", real_tick_ms);
        }

        // 允许重复 init: 所有状态一起重置, 否则会残留上一个周期的数据
        wheels_map_.clear();
        temp_timers_.clear();
        longtime_timers_.clear();
        cancelled_.clear();
        timer_idx_   = 0;
        total_ticks_ = 0;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_ops_.clear();
        }

        for (uint16_t i = 1; i <= MAX_TIMER_LEVEL; i++)
        {
            wheels_map_.emplace(i, std::make_shared<TimerWheel>(TIMER_SLOT_SIZE, i, real_tick_ms));
        }

        running_ = true;

        LOG_INFO("[TimerManager][init] tick_ms={}, wheel levels={}, slot_size={}, max_range_ms={}", real_tick_ms,
                 MAX_TIMER_LEVEL, TIMER_SLOT_SIZE, real_tick_ms * intPow(TIMER_SLOT_SIZE, MAX_TIMER_LEVEL));
    }

    uint64_t TimerManager::generateTimerIdx()
    {
        return ++timer_idx_;
    }

    uint64_t TimerManager::runAt(uint64_t expire, TimerCallBack cb)
    {
        if (!cb)
            return INVALID_TIMER_ID;

        if (expire == 0)
            expire = getNowMilliSecond();

        TimerPtr timer = std::make_shared<Timer>(generateTimerIdx(), expire, cb, TimerType::Repeated);
        if (!timer)
            return INVALID_TIMER_ID;

        timer->setRunCount(1);
        timer->setInterval(0);

        // 只入队, 不直接碰时间轮: 时间轮只在 tick 线程被触碰(见 ADR-0002)
        pushPendingOp({false, timer, 0});
        return timer->getTimerId();
    }

    uint64_t TimerManager::runAfter(const uint64_t delay, TimerCallBack cb)
    {
        return runAt(getNowMilliSecond() + delay, cb);
    }

    uint64_t TimerManager::runEvery(uint64_t interval, TimerCallBack cb, const uint32_t runcount)
    {
        if (!cb || !interval || !runcount)
            return INVALID_TIMER_ID;

        TimerPtr timer
            = std::make_shared<Timer>(generateTimerIdx(), getNowMilliSecond() + interval, cb, TimerType::Repeated);
        if (!timer)
            return INVALID_TIMER_ID;

        timer->setRunCount(runcount);
        timer->setInterval(interval);

        pushPendingOp({false, timer, 0});
        return timer->getTimerId();
    }

    uint64_t TimerManager::runForever(uint64_t interval, TimerCallBack cb)
    {
        if (!cb || !interval)
            return INVALID_TIMER_ID;

        TimerPtr timer
            = std::make_shared<Timer>(generateTimerIdx(), getNowMilliSecond() + interval, cb, TimerType::Infinited);
        if (!timer)
            return INVALID_TIMER_ID;

        timer->setRunCount(0);
        timer->setInterval(interval);

        pushPendingOp({false, timer, 0});
        return timer->getTimerId();
    }

    bool TimerManager::cancel(const uint64_t timer_id)
    {
        if (timer_id == INVALID_TIMER_ID)
            return false;

        pushPendingOp({true, nullptr, timer_id});
        return true;
    }

    void TimerManager::pushPendingOp(PendingOp op)
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_ops_.emplace_back(std::move(op));
    }

    void TimerManager::applyPendingOps()
    {
        // 双缓冲 swap: 临界区里只做一次指针交换, 业务线程不会被批量入轮的耗时阻塞
        std::vector<PendingOp> ops;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            ops.swap(pending_ops_);
        }

        for (auto& op : ops)
        {
            if (op.is_cancel)
            {
                cancelled_.insert(op.timer_id);
                continue;
            }

            if (!op.timer)
                continue;

            const uint64_t timer_id = op.timer->getTimerId();
            if (cancelled_.erase(timer_id) > 0)
            {
                // 入队之后、收割之前就被取消了, 直接丢弃
                continue;
            }

            addTimer(op.timer);
        }
    }

    bool TimerManager::isCancelled(const uint64_t timer_id) const
    {
        return cancelled_.find(timer_id) != cancelled_.end();
    }

    void TimerManager::eraseCancelled(const uint64_t timer_id)
    {
        cancelled_.erase(timer_id);
    }

    void TimerManager::addTimer(TimerPtr timer)
    {
        if (!timer)
            return;

        bool           is_add    = false;
        const uint64_t now_ms    = getNowMilliSecond();
        const uint64_t expire_ms = timer->getExpireMilliSec() > now_ms ? timer->getExpireMilliSec() - now_ms : 0;
        for (auto& item : wheels_map_)
        {
            TimerWheelPtr wheel = item.second;
            if (!wheel)
                continue;

            if (expire_ms >= wheel->getTimeRange())
                continue;

            wheel->addTimer(timer, total_ticks_);
            is_add = true;
            break;
        }

        if (!is_add)
        {
            // 放入超长定时去里列表, 等最高级轮走完一圈再重新入轮
            // 用 DEBUG 而不是 ERROR: 这是正常路径, 每次都会走一遍, 打 ERROR 是噪音
            longtime_timers_.emplace_back(timer);
            LOG_DEBUG("[TimerManager][addTimer] timer expire is too long, expire:{}", expire_ms);
        }
    }

    TimerWheelPtr TimerManager::getTimerWheel(const uint16_t level)
    {
        auto iter = wheels_map_.find(level);
        if (iter == wheels_map_.end())
            return nullptr;

        return iter->second;
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

    void TimerManager::tick()
    {
        if (!running_.load())
            return;

        LOG_TRACE("[TimerManager][timer_update]");
        TimerWheelPtr wheel = getTimerWheel(1);
        if (wheel)
        {
            wheel->tick();
        }

        // 必须在 wheel->tick() 之后、temp 重新入轮之前自增, 这样 total_ticks_ 与各级 cur_slot_ 始终一致:
        // cur_slot_(level L) == total_ticks_ / slot_size_^(L-1) % slot_size_
        ++total_ticks_;

        for (auto& item : temp_timers_)
        {
            if (!item)
                continue;

            addTimer(item);
        }
        temp_timers_.clear();

        // 必须放在最后: 此时 total_ticks_ 已经自增, cur_slot_ 也推进到位, 两者描述的都是"下一拍"的状态,
        // addTimer 里 delay = nextslot * milli_per_tick - rem 才成立.
        // 放在 tick() 开头的话, total_ticks_ 还停在上一拍, 整批新定时器会整体提前一拍触发(见 ADR-0002)
        applyPendingOps();
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

    void TimerManager::stop()
    {
        running_ = false;

        // 丢弃还没来得及入轮的注册/取消请求.
        // 这里不去清时间轮: stop() 可能由非 tick 线程(信号/主线程)调用, 直接改 wheels_map_ / temp_timers_
        // 会破坏 ADR-0002 的单线程亲和. 轮子里剩下的定时器因为 tick() 直接返回而不再触发, 随单例析构释放.
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_ops_.clear();
        }

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
                "\tms_per_tick={}, timerange={}",
                wheel->getLevel(), wheel->getSlotSize(), wheel->getCurSlot(), wheel->getMaxTick(),
                wheel->getMilliPerTick(), wheel->getTimeRange());
        }
    }
}  // namespace cncpp
#include "Global.h"
#include "TimeUtils.h"

namespace cncpp
{
    // std::shared_ptr<spdlog::logger> dailylogger = nullptr;
    // Random rnd(0xdeadbeef);
    Random   rnd(time(NULL));
    uint64_t process_tick_ms = getNowMilliSecond();

    uint32_t random()
    {
        static Random rnd_(0xdeadbeef);
        return rnd_.Next();
    }

    uint64_t getProcessTickMs()
    {
        return process_tick_ms;
    }
}  // namespace cncpp
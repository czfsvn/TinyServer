#ifndef __TIME_USEAGE_20220116_H__
#define __TIME_USEAGE_20220116_H__

#include <chrono>
#include <iostream>

#define BLOCK_COST cncpp::BlockCost __cost__(__FUNCTION__, __LINE__)

namespace cncpp
{
    using namespace std::chrono;
    class TimeUseage
    {
    public:
        TimeUseage() : begin(high_resolution_clock::now())
        {
        }

        void reset()
        {
            begin = high_resolution_clock::now();
        }

        // 默认输出毫秒
        template <typename Duartion = milliseconds>
        int64_t elapsed() const
        {
            return duration_cast<Duartion>(high_resolution_clock::now() - begin).count();
        }

        // 输出纳秒
        int64_t elapsed_nano() const
        {
            return elapsed<nanoseconds>();
        }
        // 输出微秒
        int64_t elapsed_micro() const
        {
            return elapsed<microseconds>();
        }

        // 输出秒
        int64_t elapsed_seconds() const
        {
            return elapsed<seconds>();
        }

        // 输出分
        int64_t elapsed_minutes() const
        {
            return elapsed<minutes>();
        }

        // 输出时
        int64_t elapsed_hours() const
        {
            return elapsed<hours>();
        }

    private:
        time_point<high_resolution_clock> begin = {};
    };

    class BlockCost
    {
    public:
        BlockCost(const char* func, uint32_t lines) : func_name(func), line(lines)
        {
        }

        ~BlockCost()
        {
            std::cout << func_name << ":" << line << ", \tcost: [" << timer.elapsed_micro() << "] \tmicro sec"
                      << std::endl;
        }

    private:
        std::string func_name = {};
        uint32_t line = 0;
        TimeUseage timer = {};
    };
}  // namespace cncpp

#endif
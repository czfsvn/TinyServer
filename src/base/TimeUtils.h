#pragma once

#include <time.h>
#include <chrono>
#include <iostream>

namespace cncpp
{
    using namespace std::chrono;
    const static uint32_t kMinuteSeconds = 60;
    const static uint32_t kHourSeconds   = 3600;
    const static uint32_t kDaySeconds    = 24 * kHourSeconds;

    // 获取当前纳秒数
    uint64_t inline getNowNanoSecond()
    {
        return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    }

    // 获取当前微秒数
    uint64_t inline getNowMicroSecond()
    {
        return getNowNanoSecond() / 1000;
    }

    // 获取当前毫秒数
    uint64_t inline getNowMilliSecond()
    {
        return getNowMicroSecond() / 1000;
    }

    // 获取当前秒数
    uint64_t inline getNowSecond()
    {
        return getNowMilliSecond() / 1000;
    }

    // 转换为纳秒数
    uint64_t inline getNanoSecond(const std::chrono::steady_clock::time_point& tp)
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(tp.time_since_epoch()).count();
    }

    // 转换为微秒数
    uint64_t inline getMicroSecond(const std::chrono::steady_clock::time_point& tp)
    {
        return duration_cast<microseconds>(tp.time_since_epoch()).count();
    }

    // 转换为毫秒数
    uint64_t inline getMilliSecond(const std::chrono::steady_clock::time_point& tp)
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(tp.time_since_epoch()).count();
    }

    // 转换为秒数
    uint64_t inline getSecond(const std::chrono::steady_clock::time_point& tp)
    {
        return duration_cast<seconds>(tp.time_since_epoch()).count();
    }

    uint64_t inline getMsDiff(const std::chrono::steady_clock::time_point& t1,
                              const std::chrono::steady_clock::time_point& t2)
    {
        const uint64_t t1_ms = getMilliSecond(t1);
        const uint64_t t2_ms = getMilliSecond(t2);
        if (t1_ms > t2_ms)
        {
            return t1_ms - t2_ms;
        }
        else
        {
            return t2_ms - t1_ms;
        }
        return 0;
    }

    // 获取真实时间
    inline uint64_t getRealTimeSec()
    {
        // todo: 注意时区转换
        struct timespec tv;
        clock_gettime(CLOCK_REALTIME, &tv);
        return tv.tv_sec + tv.tv_nsec / 1000000000l;
    }

    // 获取时区
    inline int32_t getTimeZone()
    {
        return -8 * 60 * 60;
    }

    // 时间点 --> 本地时间
    inline void getLocalTime(struct tm& tv, time_t t)
    {
        t -= getTimeZone();
        gmtime_r(&t, &tv);
    }

    // 时间点 --> UTC 时间
    inline void getGMTime(struct tm& tv, time_t t)
    {
        gmtime_r(&t, &tv);
    }

    // 从纪元到指定时间点经历过的零点(本地时间)
    inline int32_t localDaysFromEpoch(time_t t)
    {
        return (t - getTimeZone()) / kDaySeconds;
    }

    // 从纪元到指定时间点经历过的周一零点数量(本地时间)
    inline int32_t localWeeksFromEpoch(time_t t)
    {
        return (localDaysFromEpoch(t) + 3) / 7;
    }

    // 两个时间点相隔的零点数量 (本地时间)
    inline uint32_t localDaysBetween(time_t t1, time_t t2)
    {
        uint32_t days1 = localDaysFromEpoch(t1);
        uint32_t days2 = localDaysFromEpoch(t2);
        return ((days1 > days2) ? (days1 - days2) : (days2 - days1));
    }

    // 两个时间点相隔的周一数量 (本地时间)
    inline uint32_t localWeeksBetween(time_t t1, time_t t2)
    {
        uint32_t days1 = localWeeksFromEpoch(t1);
        uint32_t days2 = localWeeksFromEpoch(t2);
        return ((days1 > days2) ? (days1 - days2) : (days2 - days1));
    }

    inline bool isSameLocalHour(time_t t1, time_t t2)
    {
        struct tm tv1;
        struct tm tv2;
        getLocalTime(tv1, t1);
        getLocalTime(tv2, t2);

        return (tv1.tm_year == tv2.tm_year && tv1.tm_mon == tv2.tm_mon && tv1.tm_mday == tv2.tm_mday
                && tv1.tm_hour == tv2.tm_hour);
    }

    inline bool isSameLocalDay(time_t t1, time_t t2)
    {
        return localDaysFromEpoch(t1) == localDaysFromEpoch(t2);
    }

    inline bool isSameLocalWeek(time_t t1, time_t t2)
    {
        return localWeeksFromEpoch(t1) == localWeeksFromEpoch(t2);
    }

    inline bool isSameLocalMonth(time_t t1, time_t t2)
    {
        struct tm tv1;
        struct tm tv2;
        getLocalTime(tv1, t1);
        getLocalTime(tv2, t2);

        return (tv1.tm_year == tv2.tm_year && tv1.tm_mon == tv2.tm_mon);
    }

    inline bool isSameLocalYear(time_t t1, time_t t2)
    {
        struct tm tv1;
        struct tm tv2;
        getLocalTime(tv1, t1);
        getLocalTime(tv2, t2);

        return (tv1.tm_year == tv2.tm_year);
    }

    inline uint16_t getLocalHour(time_t t)
    {
        struct tm tv;
        getLocalTime(tv, t);
        return tv.tm_hour;
    }

    inline uint64_t getLocalHourSeconds(time_t t)
    {
        struct tm tv;
        getLocalTime(tv, t);
        return tv.tm_hour * kHourSeconds;
    }

    // 获取今日 0 点秒数
    inline time_t getLocalDayZero(time_t t)
    {
        return localDaysFromEpoch(t) * kDaySeconds + getTimeZone();
    }

    // 获取本周周一 0 点秒数
    inline time_t getLocalMondayZero(time_t t)
    {
        struct tm tv;
        getLocalTime(tv, t);

        uint32_t days = tv.tm_wday ? tv.tm_wday : 7;
        return getLocalDayZero(t) - (days - 1) * kDaySeconds;
    }

    // 获取下周周一 0 点秒数
    inline time_t getLocalNextMondayZero(time_t t)
    {
        return getLocalMondayZero(t) + 7 * kDaySeconds;
    }

    // retrun [0,1,2,3,4,5,6]
    inline uint8_t getLocalWeekday()
    {
        struct tm tv;
        getLocalTime(tv, getRealTimeSec());
        return tv.tm_wday;
    }

    inline uint32_t getDaySeconds()
    {
        struct tm tv;
        getLocalTime(tv, getRealTimeSec());
        return tv.tm_hour * kHourSeconds + tv.tm_min * kMinuteSeconds + tv.tm_sec;
    }

    static char* timeToStr(const time_t& t)
    {
        static char time_str[32] = {};
        struct tm   tv;
        getLocalTime(tv, t);
        strftime(time_str, 32, "%Y-%m-%d %H:%M:%S", &tv);
        return time_str;
    }

    static char* timeToCHNStr(const time_t& t)
    {
        static char time_str[32] = {};
        struct tm   tv;
        getLocalTime(tv, t);
        strftime(time_str, 32, "%Y年-%m月-%d日 %H:%M:%S", &tv);
        return time_str;
    }

    static char* timeToHMS(const time_t& t)
    {
        static char time_str[32] = {};
        struct tm   tv;
        getLocalTime(tv, t);
        strftime(time_str, 32, "%H:%M:%S", &tv);
        return time_str;
    }
}  // namespace cncpp

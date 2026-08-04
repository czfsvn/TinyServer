

#pragma once

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "config.h"
#include "singleton.h"

namespace cncpp
{
    // 轮转粒度
    enum class RotationPeriod
    {
        Minute,
        Hour,
        Day
    };
    // 对齐方式：wall_clock 对齐自然边界；rolling 从启动起按 interval 滚动
    enum class RotationMode
    {
        WallClock,
        Rolling
    };

    struct FileRotationConfig
    {
        RotationPeriod period   = RotationPeriod::Hour;
        RotationMode   mode     = RotationMode::WallClock;
        int            interval = 1;        // rolling 模式下每 N 个 period 轮转一次
        std::string    daily_at = "00:00";  // day + wall_clock 时每日轮转时刻（HH:MM）
    };

    struct LogConfig
    {
        // 全局
        std::string global_level        = "info";
        std::string format              = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [tid %t] %v";
        std::string flush_on            = "error";
        int         flush_interval_secs = 3;

        // 异步引擎
        struct AsyncCfg
        {
            bool        enabled         = true;
            std::size_t queue_size      = 8192;
            std::size_t thread_count    = 1;
            std::string overflow_policy = "block";  // block | overrun_oldest
        } async;

        // 控制台通道
        struct ConsoleCfg
        {
            bool        enabled = true;
            std::string level   = "info";
        } console;

        // 文件通道
        struct FileCfg
        {
            bool               enabled         = true;
            std::string        level           = "debug";
            std::string        dir             = "./logs";
            std::string        filename_prefix = "app";
            std::string        filename_suffix = ".log";
            FileRotationConfig rotation;
        } file;

        // 从 YAML/JSON 配置文件加载（按扩展名选择解析器；当前实现 YAML）
        static LogConfig from_file(const std::string& path);
    };

    // 按时间轮转的文件 sink：
    //   - period(minute|hour|day) 决定轮转粒度
    //   - mode(wall_clock|rolling) 决定对齐方式
    //   - 文件名 = dir / prefix "_" bucket suffix
    //   - 跨 bucket 才新建文件；无日志的 bucket 不会产生空文件
    //   - 线程安全由 base_sink<Mutex> 的锁保证
    template <typename Mutex>
    class time_rotating_file_sink final : public spdlog::sinks::base_sink<Mutex>
    {
    public:
        time_rotating_file_sink(const std::string& dir, const std::string& prefix, const std::string& suffix,
                                RotationPeriod period, RotationMode mode, int interval, const std::string& daily_at)
            : dir_(dir),
              prefix_(prefix),
              suffix_(suffix),
              period_(period),
              mode_(mode),
              interval_(std::max(1, interval)),
              daily_at_(daily_at),
              start_time_(std::chrono::system_clock::now()),
              start_bucket_(wall_clock_bucket(start_time_))
        {
            std::error_code ec;
            std::filesystem::create_directories(dir_, ec);  // 目录已存在不报错
            rotate_if_needed(start_time_);                  // 打开首个文件
        }

    protected:
        void sink_it_(const spdlog::details::log_msg& msg) override
        {
            auto now = std::chrono::system_clock::now();
            rotate_if_needed(now);
            if (!file_)
                return;
            spdlog::memory_buf_t formatted;
            this->formatter_->format(msg, formatted);  // 使用 logger 下发的 pattern
            file_->write(formatted);
        }

        void flush_() override
        {
            if (file_)
                file_->flush();
        }

    private:
        // 计算当前时刻所属 bucket
        std::string current_bucket(std::chrono::system_clock::time_point now) const
        {
            if (mode_ == RotationMode::WallClock)
            {
                return wall_clock_bucket(now);
            }
            // rolling：从启动起每 interval 个 period 轮转一次
            auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
            long sp          = seconds_per_period();
            long idx         = static_cast<long>(elapsed_sec) / (sp * interval_);
            return start_bucket_ + "_" + std::to_string(idx);
        }

        // wall_clock 模式的 bucket：直接取自然时间格式化
        std::string wall_clock_bucket(std::chrono::system_clock::time_point tp) const
        {
            std::time_t t  = std::chrono::system_clock::to_time_t(tp);
            std::tm     tm = local_time(t);
            char        buf[24];
            switch (period_)
            {
                case RotationPeriod::Minute:
                    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M", &tm);
                    break;
                case RotationPeriod::Day:
                    // daily_at：若未到当天轮转时刻，归属前一天
                    if (!daily_at_.empty())
                    {
                        int hh = 0, mm = 0;
                        std::sscanf(daily_at_.c_str(), "%d:%d", &hh, &mm);
                        std::tm rt = tm;
                        rt.tm_hour = hh;
                        rt.tm_min  = mm;
                        rt.tm_sec  = 0;
                        if (t < std::mktime(&rt))
                        {
                            t -= 86400;
                            tm = local_time(t);
                        }
                    }
                    std::strftime(buf, sizeof(buf), "%Y%m%d", &tm);
                    break;
                case RotationPeriod::Hour:
                default:
                    std::strftime(buf, sizeof(buf), "%Y%m%d_%H", &tm);
                    break;
            }
            return buf;
        }

        long seconds_per_period() const
        {
            switch (period_)
            {
                case RotationPeriod::Minute:
                    return 60;
                case RotationPeriod::Day:
                    return 86400;
                case RotationPeriod::Hour:
                default:
                    return 3600;
            }
        }

        // 跨 bucket 则关闭旧文件、打开新文件
        void rotate_if_needed(std::chrono::system_clock::time_point now)
        {
            std::string bucket = current_bucket(now);
            if (bucket == current_bucket_)
                return;  // 同一 bucket，继续写
            if (file_)
                file_->close();
            current_bucket_      = bucket;
            std::string filename = dir_ + "/" + prefix_ + "_" + bucket + suffix_;
            file_                = std::make_unique<spdlog::details::file_helper>();
            file_->open(filename, false);  // false = 追加：重启后同 bucket 文件可续写
        }

        static std::tm local_time(std::time_t t)
        {
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            return tm;
        }

        std::string                                   dir_, prefix_, suffix_, daily_at_;
        RotationPeriod                                period_;
        RotationMode                                  mode_;
        int                                           interval_;
        std::chrono::system_clock::time_point         start_time_;
        std::string                                   start_bucket_;
        std::string                                   current_bucket_;
        std::unique_ptr<spdlog::details::file_helper> file_;
    };

    // 全局日志门面：持有 spdlog::logger，聚合 ConsoleSink + TimeRotatingFileSink，
    // 隐藏 spdlog 底层细节，对外提供统一 API 与编程式级别切换。
    class Logger : public cncpp::Singleton<Logger>
    {
    public:  // 用配置初始化：构建 sinks / 异步 logger / flush 策略。进程内调用一次。
        bool init(const std::string& log_yml_path);

        // 请求关闭：仅设置标志，让 LOG_ 宏返回 nullptr（不释放资源）
        // 用于在清理线程池之前先停止日志输出
        void requestShutdown()
        {
            shutdown_requested_.store(true);
        }

        // 完全关闭：释放 logger 资源并关闭 spdlog 线程池
        void shutdown();

        // 编程式动态切换（与配置热加载共用同一条 apply 路径，level 为 atomic，线程安全）
        void set_global_level(spdlog::level::level_enum lvl);
        void set_console_level(spdlog::level::level_enum lvl);
        void set_file_level(spdlog::level::level_enum lvl);

        // 供宏调用，未 init 或已 shutdown 时返回 nullptr
        spdlog::logger* get()
        {
            if (shutdown_requested_.load())
                return nullptr;
            return logger_.get();
        }

        // 检查是否已初始化且未关闭
        bool is_ready() const
        {
            return logger_ && !shutdown_requested_.load();
        }

    private:
        friend class Singleton<Logger>;
        Logger() {};
        std::shared_ptr<spdlog::logger> logger_;
        spdlog::sink_ptr                console_sink_;
        spdlog::sink_ptr                file_sink_;
        std::atomic<bool>               shutdown_requested_{false};
        std::atomic<bool>               shutdown_done_{false};  // shutdown() 幂等保护
    };
}  // namespace cncpp

// 统一日志 API：隐藏 spdlog，未 init 时为空操作（安全）
#define sLogger cncpp::Logger::getMe()

#define MYLOG_INIT(cfg) cncpp::Logger::getMe().init(cfg)
#define MYLOG_SHUTDOWN() cncpp::Logger::getMe().shutdown()

#define LOG_TRACE(...)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        auto* l = cncpp::Logger::getMe().get();                                                                        \
        if (l)                                                                                                         \
            l->trace(__VA_ARGS__);                                                                                     \
    } while (0)

#define LOG_DEBUG(...)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        auto* l = cncpp::Logger::getMe().get();                                                                        \
        if (l)                                                                                                         \
            l->debug(__VA_ARGS__);                                                                                     \
    } while (0)

#define LOG_INFO(...)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        auto* l = cncpp::Logger::getMe().get();                                                                        \
        if (l)                                                                                                         \
            l->info(__VA_ARGS__);                                                                                      \
    } while (0)

#define LOG_WARN(...)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        auto* l = cncpp::Logger::getMe().get();                                                                        \
        if (l)                                                                                                         \
            l->warn(__VA_ARGS__);                                                                                      \
    } while (0)

#define LOG_ERROR(...)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        auto* l = cncpp::Logger::getMe().get();                                                                        \
        if (l)                                                                                                         \
            l->error(__VA_ARGS__);                                                                                     \
    } while (0)

// FATAL / CRITICAL 映射到 spdlog::critical
#define LOG_FATAL(...)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        auto* l = cncpp::Logger::getMe().get();                                                                        \
        if (l)                                                                                                         \
            l->critical(__VA_ARGS__);                                                                                  \
    } while (0)

#define LOG_CRITICAL(...)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        auto* l = cncpp::Logger::getMe().get();                                                                        \
        if (l)                                                                                                         \
            l->critical(__VA_ARGS__);                                                                                  \
    } while (0)

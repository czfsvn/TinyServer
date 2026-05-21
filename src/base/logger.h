#pragma once

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "config.h"
//#include "singleton.h"

namespace cncpp
{

    // 日志级别枚举 - 避免与系统宏冲突
    enum class LogLevel
    {
        TRACE    = SPDLOG_LEVEL_TRACE,
        DBG      = SPDLOG_LEVEL_DEBUG,
        LOG_INFO = SPDLOG_LEVEL_INFO,
        WARN     = SPDLOG_LEVEL_WARN,
        ERROR    = SPDLOG_LEVEL_ERROR,
        CRIT     = SPDLOG_LEVEL_CRITICAL,
        OFF      = SPDLOG_LEVEL_OFF
    };

    // 滚动策略
    enum class RotationPolicy
    {
        HOURLY,  // 按小时
        DAILY    // 按日
    };

    // 日志配置结构
    /*
    struct LoggerConfig
    {
        std::string log_dir = "logs";  // 日志目录
        std::string app_name = "app";  // 应用名称
        std::string log_file = "app.log";
        RotationPolicy rotation_policy = RotationPolicy::DAILY;
        int retention_days = 30;                   // 保留天数（按日）
        int retention_hours = 72;                  // 保留小时数（按小时）
        size_t max_file_size = 1024 * 1024 * 100;  // 单个文件最大大小（默认100MB）
        bool enable_console = true;
        bool async_mode = false;
        size_t async_queue_size = 8192;
        LogLevel console_level = LogLevel::DBG;
        LogLevel file_level = LogLevel::LOG_INFO;
        std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%t%$] [%^%l%$] %v";
    };
    */

    inline LogLevel parseLogLevel(const std::string& level_str)
    {
        std::string lower = level_str;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "trace")
            return LogLevel::TRACE;
        if (lower == "debug" || lower == "dbg")
            return LogLevel::DBG;
        if (lower == "info")
            return LogLevel::LOG_INFO;
        if (lower == "warn" || lower == "warning")
            return LogLevel::WARN;
        if (lower == "error" || lower == "err")
            return LogLevel::ERROR;
        if (lower == "critical" || lower == "crit")
            return LogLevel::CRIT;
        if (lower == "off")
            return LogLevel::OFF;

        // 默认级别
        return LogLevel::LOG_INFO;
    }

    inline std::string parseLogLevelStr(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::TRACE:
                return "trace";
            case LogLevel::DBG:
                return "debug";
            case LogLevel::LOG_INFO:
                return "info";
            case LogLevel::WARN:
                return "warn";
            case LogLevel::ERROR:
                return "error";
            case LogLevel::CRIT:
                return "critical";
            case LogLevel::OFF:
                return "off";
            default:
                return "info";
        }
    }

    // 按小时滚动的 sink（使用组合方式，不继承 final 类）
    class hourly_file_sink_mt : public spdlog::sinks::sink
    {
    public:
        hourly_file_sink_mt(const std::string& base_filename, int retention_hours)
            : _base_filename(base_filename), _retention_hours(retention_hours)
        {
            rotate_file();
            start_cleanup_thread();
        }

        ~hourly_file_sink_mt()
        {
            // 使用新方法停止线程，确保线程能被立即唤醒
            stop_cleanup_thread();
            if (_cleanup_thread.joinable())
            {
                _cleanup_thread.join();
            }
        }

        void log(const spdlog::details::log_msg& msg) override
        {
            check_rotation();
            if (_current_sink)
            {
                _current_sink->log(msg);
            }
        }

        void flush() override
        {
            if (_current_sink)
            {
                _current_sink->flush();
            }
        }

        void set_pattern(const std::string& pattern) override
        {
            _pattern = pattern;
            if (_current_sink)
            {
                _current_sink->set_pattern(pattern);
            }
        }

        void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override
        {
            if (_current_sink)
            {
                _current_sink->set_formatter(std::move(sink_formatter));
            }
        }

        void set_level(spdlog::level::level_enum log_level)
        {
            _level = log_level;
            if (_current_sink)
            {
                _current_sink->set_level(log_level);
            }
        }

        spdlog::level::level_enum level() const
        {
            return _level;
        }

        bool should_log(spdlog::level::level_enum msg_level) const
        {
            return msg_level >= _level;
        }

    private:
        void check_rotation()
        {
            auto      now        = std::chrono::system_clock::now();
            auto      now_time_t = std::chrono::system_clock::to_time_t(now);
            struct tm tm_buf;
            localtime_r(&now_time_t, &tm_buf);

            int current_hour = tm_buf.tm_hour;
            int current_day  = tm_buf.tm_mday;

            if (current_hour != _last_hour || current_day != _last_day)
            {
                rotate_file();
                _last_hour = current_hour;
                _last_day  = current_day;
            }
        }

        void rotate_file()
        {
            std::lock_guard<std::mutex> lock(_mutex);

            std::string filename = get_current_filename();

            // 创建新的文件 sink
            auto new_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename);
            new_sink->set_level(_level);

            if (!_pattern.empty())
            {
                new_sink->set_pattern(_pattern);
            }

            _current_sink = new_sink;
        }

        std::string get_current_filename()
        {
            auto      now        = std::chrono::system_clock::now();
            auto      now_time_t = std::chrono::system_clock::to_time_t(now);
            struct tm tm_buf;
            localtime_r(&now_time_t, &tm_buf);

            std::string base = _base_filename;

            // 如果文件名已有 .log 后缀，去掉它
            size_t pos = base.rfind(".log");
            if (pos != std::string::npos && pos == base.length() - 4)
            {
                base = base.substr(0, pos);
            }

            char filename[512];
            std::snprintf(filename, sizeof(filename), "%s_%04d%02d%02d_%02d.log", base.c_str(), tm_buf.tm_year + 1900,
                          tm_buf.tm_mon + 1, tm_buf.tm_mday, tm_buf.tm_hour);

            return std::string(filename);
        }

        void start_cleanup_thread()
        {
            _cleanup_thread = std::thread([this]() {
                while (!_stop_cleanup)
                {
                    std::unique_lock<std::mutex> lock(_cleanup_mutex);
                    // 使用条件变量等待，最多等待1小时
                    _cleanup_cv.wait_for(lock, std::chrono::hours(1), [this]() {
                        return _stop_cleanup.load();
                    });

                    if (_stop_cleanup)
                    {
                        break;
                    }

                    cleanup_old_logs();
                }
            });
        }

        void stop_cleanup_thread()
        {
            _stop_cleanup = true;
            _cleanup_cv.notify_one();
        }

        void cleanup_old_logs()
        {
            try
            {
                std::string dir = _base_filename.substr(0, _base_filename.find_last_of("/\\") + 1);
                if (dir.empty())
                    dir = "./";

                auto now       = std::chrono::system_clock::now();
                auto now_hours = std::chrono::duration_cast<std::chrono::hours>(now.time_since_epoch()).count();

                for (const auto& entry : std::filesystem::directory_iterator(dir))
                {
                    if (entry.is_regular_file())
                    {
                        auto ftime = entry.last_write_time();
                        auto file_hours
                            = std::chrono::duration_cast<std::chrono::hours>(ftime.time_since_epoch()).count();

                        if ((now_hours - file_hours) > static_cast<long long>(_retention_hours))
                        {
                            std::error_code ec;
                            std::filesystem::remove(entry.path(), ec);
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                // 静默失败，避免影响主流程
            }
        }

        std::string                                        _base_filename;
        int                                                _retention_hours;
        std::shared_ptr<spdlog::sinks::basic_file_sink_mt> _current_sink;
        std::string                                        _pattern;
        spdlog::level::level_enum                          _level = spdlog::level::info;
        std::atomic<int>                                   _last_hour{-1};
        std::atomic<int>                                   _last_day{-1};
        std::mutex                                         _mutex;
        std::thread                                        _cleanup_thread;
        std::atomic<bool>                                  _stop_cleanup{false};
        std::mutex                                         _cleanup_mutex;
        std::condition_variable                            _cleanup_cv;
    };

    class MyLogger : public Singleton<MyLogger>
    {
    public:
        bool init()
        {
            std::lock_guard<std::mutex> lock(_mutex);

            if (_initialized)
            {
                return true;
            }

            try
            {
                // 创建日志目录
                if (!std::filesystem::exists(sLoggerConfig.log_dir()))
                {
                    std::filesystem::create_directories(sLoggerConfig.log_dir());
                }
                // std::filesystem::create_directories(config.log_dir);

                // 构建完整的日志文件路径
                std::string full_path = sLoggerConfig.log_dir() + "/" + sLoggerConfig.log_file();

                // 创建 sinks
                std::vector<spdlog::sink_ptr> sinks;

                // 1. 创建文件 sink（按策略）
                if (sLoggerConfig.rotation_policy() == "HOURLY")
                {
                    // 按小时滚动：使用自定义的 hourly_file_sink_mt
                    auto hourly_sink
                        = std::make_shared<hourly_file_sink_mt>(full_path, sLoggerConfig.retention_hours());
                    hourly_sink->set_level(
                        static_cast<spdlog::level::level_enum>(parseLogLevel(sLoggerConfig.file_level())));
                    sinks.push_back(hourly_sink);
                }
                else
                {
                    // 按日滚动：使用 daily_file_sink
                    auto daily_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                        full_path, 0, 0, false, sLoggerConfig.retention_days());
                    daily_sink->set_level(
                        static_cast<spdlog::level::level_enum>(parseLogLevel(sLoggerConfig.file_level())));
                    sinks.push_back(daily_sink);
                }

                // 2. 创建控制台 sink（如果需要）
                if (sLoggerConfig.enable_console())
                {
                    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                    console_sink->set_level(
                        static_cast<spdlog::level::level_enum>(parseLogLevel(sLoggerConfig.console_level())));

                    // 设置不同级别的颜色
                    console_sink->set_color(spdlog::level::trace, console_sink->white);
                    console_sink->set_color(spdlog::level::debug, console_sink->cyan);
                    console_sink->set_color(spdlog::level::info, console_sink->green);
                    console_sink->set_color(spdlog::level::warn, console_sink->yellow);
                    console_sink->set_color(spdlog::level::err, console_sink->red);
                    console_sink->set_color(spdlog::level::critical, console_sink->red_bold);

                    sinks.push_back(console_sink);
                }

                // 创建 logger
                if (sLoggerConfig.async_mode())
                {
                    spdlog::init_thread_pool(sLoggerConfig.async_queue_size(), 1);
                    _logger = std::make_shared<spdlog::async_logger>(sLoggerConfig.app_name(), sinks.begin(),
                                                                     sinks.end(), spdlog::thread_pool(),
                                                                     spdlog::async_overflow_policy::block);
                }
                else
                {
                    _logger = std::make_shared<spdlog::logger>(sLoggerConfig.app_name(), sinks.begin(), sinks.end());
                }

                // 设置格式
                _logger->set_pattern(sLoggerConfig.pattern.get());
                _logger->set_level(spdlog::level::trace);

                // 设置刷新策略
                _logger->flush_on(spdlog::level::err);
                spdlog::flush_every(std::chrono::seconds(3));

                // 注册为默认 logger
                spdlog::set_default_logger(_logger);

                _initialized = true;

                SPDLOG_LOGGER_INFO(_logger, "Logger initialized successfully");
                SPDLOG_LOGGER_INFO(_logger, "Log directory: {}", sLoggerConfig.log_dir());
                SPDLOG_LOGGER_INFO(_logger, "Log file: {}", sLoggerConfig.log_file());
                SPDLOG_LOGGER_INFO(_logger, "Console level: {}", sLoggerConfig.console_level());
                SPDLOG_LOGGER_INFO(_logger, "File level: {}", sLoggerConfig.file_level());

                return true;
            }
            catch (const spdlog::spdlog_ex& ex)
            {
                std::fprintf(stderr, "Logger initialization failed: %s\n", ex.what());
                return false;
            }
            catch (const std::exception& ex)
            {
                std::fprintf(stderr, "Logger initialization failed: %s\n", ex.what());
                return false;
            }
        }

        std::shared_ptr<spdlog::logger> get()
        {
            std::lock_guard<std::mutex> lock(_mutex);
            return _logger;
        }

        void set_console_level(LogLevel level)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            sLoggerConfig.console_level.set(parseLogLevelStr(level));
            if (_logger)
            {
                for (auto& sink : _logger->sinks())
                {
                    if (dynamic_cast<spdlog::sinks::stdout_color_sink_mt*>(sink.get()))
                    {
                        sink->set_level(
                            static_cast<spdlog::level::level_enum>(parseLogLevel(sLoggerConfig.console_level())));
                        break;
                    }
                }
            }
        }

        void set_file_level(LogLevel level)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            sLoggerConfig.file_level.set(parseLogLevelStr(level));
            if (_logger)
            {
                for (auto& sink : _logger->sinks())
                {
                    if (!dynamic_cast<spdlog::sinks::stdout_color_sink_mt*>(sink.get()))
                    {
                        sink->set_level(
                            static_cast<spdlog::level::level_enum>(parseLogLevel(sLoggerConfig.file_level())));
                        break;
                    }
                }
            }
        }

        void shutdown()
        {
            std::lock_guard<std::mutex> lock(_mutex);

            if (_logger)
            {
                _logger->info("Logger shutting down...");
                // 刷新日志缓冲区
                _logger->flush();

                // 等待一段时间让异步日志线程完成处理
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // 先重置 logger，触发 hourly_file_sink_mt 的析构函数
                // 这样可以确保清理线程被正确停止
                _logger.reset();

                // 调用 spdlog 的 shutdown
                spdlog::shutdown();
            }
            _initialized = false;
        }

    private:
        std::shared_ptr<spdlog::logger> _logger      = nullptr;
        bool                            _initialized = false;
        std::mutex                      _mutex       = std::mutex();
    };
}  // namespace cncpp

#define sLogger cncpp::MyLogger::getMe()

#define LOG_TRACE(...) SPDLOG_LOGGER_TRACE(sLogger.get(), __VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(sLogger.get(), __VA_ARGS__)
#define LOG_INFO(...) SPDLOG_LOGGER_INFO(sLogger.get(), __VA_ARGS__)
#define LOG_WARN(...) SPDLOG_LOGGER_WARN(sLogger.get(), __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_ERROR(sLogger.get(), __VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(sLogger.get(), __VA_ARGS__)

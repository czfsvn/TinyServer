#include "logger.h"
#include <yaml-cpp/yaml.h>
#include <exception>
#include <thread>
#include "config.h"

namespace cncpp
{
    using time_rotating_file_sink_mt = time_rotating_file_sink<std::mutex>;
    using time_rotating_file_sink_st = time_rotating_file_sink<spdlog::details::null_mutex>;

    static RotationPeriod str_to_period(const std::string& s)
    {
        if (s == "minute")
            return RotationPeriod::Minute;
        if (s == "day")
            return RotationPeriod::Day;
        return RotationPeriod::Hour;  // default
    }

    static RotationMode str_to_mode(const std::string& s)
    {
        return (s == "rolling") ? RotationMode::Rolling : RotationMode::WallClock;
    }

    LogConfig LogConfig::from_file(const std::string& path)
    {
        LogConfig  cfg;
        YAML::Node root = YAML::LoadFile(path);  // 文件不存在/格式错误会抛异常，交由调用方处理
        const YAML::Node& log = root["log"];
        if (!log)
            return cfg;

        if (log["global_level"])
            cfg.global_level = log["global_level"].as<std::string>();
        if (log["format"])
            cfg.format = log["format"].as<std::string>();
        if (log["flush_on"])
            cfg.flush_on = log["flush_on"].as<std::string>();
        if (log["flush_interval_secs"])
            cfg.flush_interval_secs = log["flush_interval_secs"].as<int>();

        if (auto a = log["async"])
        {
            if (a["enabled"])
                cfg.async.enabled = a["enabled"].as<bool>();
            if (a["queue_size"])
                cfg.async.queue_size = a["queue_size"].as<std::size_t>();
            if (a["thread_count"])
                cfg.async.thread_count = a["thread_count"].as<std::size_t>();
            if (a["overflow_policy"])
                cfg.async.overflow_policy = a["overflow_policy"].as<std::string>();
        }
        if (auto c = log["console"])
        {
            if (c["enabled"])
                cfg.console.enabled = c["enabled"].as<bool>();
            if (c["level"])
                cfg.console.level = c["level"].as<std::string>();
        }
        if (auto f = log["file"])
        {
            if (f["enabled"])
                cfg.file.enabled = f["enabled"].as<bool>();
            if (f["level"])
                cfg.file.level = f["level"].as<std::string>();
            if (f["dir"])
                cfg.file.dir = f["dir"].as<std::string>();
            if (f["filename_prefix"])
                cfg.file.filename_prefix = f["filename_prefix"].as<std::string>();
            if (f["filename_suffix"])
                cfg.file.filename_suffix = f["filename_suffix"].as<std::string>();
            if (auto r = f["rotation"])
            {
                if (r["period"])
                    cfg.file.rotation.period = str_to_period(r["period"].as<std::string>());
                if (r["mode"])
                    cfg.file.rotation.mode = str_to_mode(r["mode"].as<std::string>());
                if (r["interval"])
                    cfg.file.rotation.interval = r["interval"].as<int>();
                if (r["daily_at"])
                    cfg.file.rotation.daily_at = r["daily_at"].as<std::string>();
            }
        }
        return cfg;
    }

    static spdlog::level::level_enum parse_level(const std::string& s)
    {
        if (s == "trace")
            return spdlog::level::trace;
        if (s == "debug")
            return spdlog::level::debug;
        if (s == "warn" || s == "warning")
            return spdlog::level::warn;
        if (s == "error" || s == "err")
            return spdlog::level::err;
        if (s == "fatal" || s == "critical")
            return spdlog::level::critical;  // FATAL → critical
        return spdlog::level::info;
    }

    bool Logger::init(const std::string& log_yml_path)
    {
        try
        {
            // 先重置 shutdown 标志
            shutdown_requested_.store(false);

            LogConfig                     cfg = LogConfig::from_file(log_yml_path);
            std::vector<spdlog::sink_ptr> sinks;

            if (cfg.console.enabled)
            {
                console_sink_ = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console_sink_->set_level(parse_level(cfg.console.level));
                sinks.push_back(console_sink_);
            }
            if (cfg.file.enabled)
            {
                file_sink_ = std::make_shared<time_rotating_file_sink_mt>(
                    cfg.file.dir, cfg.file.filename_prefix, cfg.file.filename_suffix, cfg.file.rotation.period,
                    cfg.file.rotation.mode, cfg.file.rotation.interval, cfg.file.rotation.daily_at);
                file_sink_->set_level(parse_level(cfg.file.level));
                sinks.push_back(file_sink_);
            }

            if (cfg.async.enabled)
            {
                spdlog::init_thread_pool(cfg.async.queue_size, cfg.async.thread_count);
                auto policy = (cfg.async.overflow_policy == "overrun_oldest")
                                  ? spdlog::async_overflow_policy::overrun_oldest
                                  : spdlog::async_overflow_policy::block;
                logger_     = std::make_shared<spdlog::async_logger>("app", sinks.begin(), sinks.end(),
                                                                     spdlog::thread_pool(), policy);
            }
            else
            {
                logger_ = std::make_shared<spdlog::logger>("app", sinks.begin(), sinks.end());
            }

            // logger level 设为最低，由各 sink 自身 level 过滤 → 双通道独立级别
            logger_->set_level(spdlog::level::trace);
            logger_->set_pattern(cfg.format);
            for (auto& s : sinks)
                s->set_pattern(cfg.format);
            logger_->flush_on(parse_level(cfg.flush_on));
            if (cfg.flush_interval_secs > 0)
                spdlog::flush_every(std::chrono::seconds(cfg.flush_interval_secs));

            spdlog::set_default_logger(logger_);
            return true;
        }
        catch (const std::exception& e)
        {
            return false;
        }
    }

    void Logger::shutdown()
    {
        // 幂等保护：整个 shutdown 流程只跑一次
        bool expected = false;
        if (!shutdown_done_.compare_exchange_strong(expected, true))
        {
            return;  // 已经调用过 shutdown 了
        }

        // 标记为"已请求关闭"，让 LOG_ 宏返回 nullptr
        shutdown_requested_.store(true);

        // 等待所有持有的 logger 引用释放（最多 500ms）
        // 这确保其他线程有足够时间完成正在进行的日志操作
        {
            auto start = std::chrono::steady_clock::now();
            while (logger_.use_count() > 1)
            {
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed >= std::chrono::milliseconds(500))
                {
                    // 超时，强制继续（可能有线程卡住了）
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        if (logger_)
            logger_->flush();

        // 从 spdlog 全局注册表中移除所有 logger，
        // 这样 spdlog 的周期性 flush 线程不会再找到任何 logger 去 flush
        spdlog::drop_all();

        // 清除 default_logger 引用
        spdlog::set_default_logger(nullptr);

        // 释放我们持有的 sinks 和 logger
        logger_.reset();
        console_sink_.reset();
        file_sink_.reset();

        // 在所有 async_logger 引用都已释放后，安全地关闭 spdlog。
        // spdlog::shutdown() 会：
        //   1. 停止 flush_every 周期性 flush 线程
        //   2. 销毁全局异步线程池
        //   3. 清空注册表
        // 此时已没有任何 async_logger 持有线程池的引用，所以是安全的。
        //
        // 关键：必须在 main() 返回前（全局静态析构开始前）调用。
        // 否则 spdlog 自身的全局静态析构顺序在不同 TU 间未定义，
        // 会导致 "async log/flush: thread pool doesn't exist anymore" 段错误。
        spdlog::shutdown();
    }

    void Logger::set_global_level(spdlog::level::level_enum lvl)
    {
        if (logger_)
            logger_->set_level(lvl);
    }
    void Logger::set_console_level(spdlog::level::level_enum lvl)
    {
        if (console_sink_)
            console_sink_->set_level(lvl);
    }
    void Logger::set_file_level(spdlog::level::level_enum lvl)
    {
        if (file_sink_)
            file_sink_->set_level(lvl);
    }
}  // namespace cncpp

module;

#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

module simnet.telemetry;

import :log;
import :types;
import simnet.config;

namespace
{
    std::mutex logger_mutex;
    std::shared_ptr<spdlog::logger> logger;

    using namespace simnet;

    [[nodiscard]] LogLevel parse_log_level(std::string_view value)
    {
        if (value == "trace")
        {
            return LogLevel::Trace;
        }
        if (value == "debug")
        {
            return LogLevel::Debug;
        }
        if (value == "info")
        {
            return LogLevel::Info;
        }
        if (value == "warn")
        {
            return LogLevel::Warn;
        }
        if (value == "error")
        {
            return LogLevel::Error;
        }
        if (value == "critical")
        {
            return LogLevel::Critical;
        }
        if (value == "off")
        {
            return LogLevel::Off;
        }
        throw std::invalid_argument("invalid telemetry log level: " + std::string{value});
    }

    [[nodiscard]] spdlog::level::level_enum to_spdlog_level(LogLevel level) noexcept
    {
        switch (level)
        {
            case LogLevel::Trace:
                return spdlog::level::trace;
            case LogLevel::Debug:
                return spdlog::level::debug;
            case LogLevel::Info:
                return spdlog::level::info;
            case LogLevel::Warn:
                return spdlog::level::warn;
            case LogLevel::Error:
                return spdlog::level::err;
            case LogLevel::Critical:
                return spdlog::level::critical;
            case LogLevel::Off:
                return spdlog::level::off;
        }
        return spdlog::level::info;
    }

    [[nodiscard]] std::string_view category_name(LogCategory category) noexcept
    {
        switch (category)
        {
            case LogCategory::Core:
                return "core";
            case LogCategory::Config:
                return "config";
            case LogCategory::Telemetry:
                return "telemetry";
            case LogCategory::Simulation:
                return "simulation";
            case LogCategory::Snapshot:
                return "snapshot";
            case LogCategory::Spatial:
                return "spatial";
            case LogCategory::Pipeline:
                return "pipeline";
            case LogCategory::Transport:
                return "transport";
            case LogCategory::Render:
                return "render";
            case LogCategory::Benchmark:
                return "benchmark";
        }
        return "unknown";
    }

    [[nodiscard]] std::shared_ptr<spdlog::logger> current_logger()
    {
        auto const lock = std::scoped_lock{logger_mutex};
        return logger;
    }
}

namespace simnet
{
    void initialize_telemetry(TelemetryConfig const& config)
    {
        auto const log_level = parse_log_level(config.min_level);
        auto sinks = std::vector<spdlog::sink_ptr>{};

        if (config.console_log_enabled)
        {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        if (config.file_log_enabled)
        {
            std::filesystem::create_directories(config.log_directory);
            sinks.push_back(
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                    std::filesystem::path{config.log_directory} / "simnet.log",
                    true
                )
            );
        }

        auto created = std::shared_ptr<spdlog::logger>{};
        if (!sinks.empty())
        {
            created = std::make_shared<spdlog::logger>("simnet", sinks.begin(), sinks.end());
            created->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
            created->set_level(to_spdlog_level(log_level));
        }

        auto const lock = std::scoped_lock{logger_mutex};
        logger = std::move(created);
    }

    void shutdown_telemetry()
    {
        std::shared_ptr<spdlog::logger> old_logger;
        {
            auto const lock = std::scoped_lock{logger_mutex};
            old_logger = std::move(logger);
        }
        if (old_logger)
        {
            old_logger->flush();
        }
    }

    void log(LogCategory category, LogLevel level, std::string_view message)
    {
        auto active_logger = current_logger();
        if (!active_logger)
        {
            return;
        }
        active_logger->log(to_spdlog_level(level), "[{}] {}", category_name(category), message);
    }

    bool log_enabled(LogLevel level)
    {
        auto active_logger = current_logger();
        return active_logger != nullptr && active_logger->should_log(to_spdlog_level(level));
    }
}

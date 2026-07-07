module;

#include <filesystem>
#include <memory>
#include <mutex>
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

    [[nodiscard]] spdlog::level::level_enum to_spdlog_level(simnet::LogLevel level) noexcept
    {
        switch (level) {
        case simnet::LogLevel::Trace:
            return spdlog::level::trace;
        case simnet::LogLevel::Debug:
            return spdlog::level::debug;
        case simnet::LogLevel::Info:
            return spdlog::level::info;
        case simnet::LogLevel::Warn:
            return spdlog::level::warn;
        case simnet::LogLevel::Error:
            return spdlog::level::err;
        case simnet::LogLevel::Critical:
            return spdlog::level::critical;
        case simnet::LogLevel::Off:
            return spdlog::level::off;
        }
        return spdlog::level::info;
    }

    [[nodiscard]] std::string_view category_name(simnet::LogCategory category) noexcept
    {
        switch (category) {
        case simnet::LogCategory::Core:
            return "core";
        case simnet::LogCategory::Config:
            return "config";
        case simnet::LogCategory::Telemetry:
            return "telemetry";
        case simnet::LogCategory::Simulation:
            return "simulation";
        case simnet::LogCategory::Snapshot:
            return "snapshot";
        case simnet::LogCategory::Spatial:
            return "spatial";
        case simnet::LogCategory::Pipeline:
            return "pipeline";
        case simnet::LogCategory::Transport:
            return "transport";
        case simnet::LogCategory::Render:
            return "render";
        case simnet::LogCategory::Benchmark:
            return "benchmark";
        }
        return "unknown";
    }

    [[nodiscard]] std::shared_ptr<spdlog::logger> make_default_logger()
    {
        auto created = spdlog::stdout_color_mt("simnet");
        created->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
        created->set_level(spdlog::level::info);
        return created;
    }

    [[nodiscard]] std::shared_ptr<spdlog::logger> current_logger()
    {
        std::scoped_lock lock { logger_mutex };
        if (!logger) {
            // Logging before initialization should still be visible.
            logger = make_default_logger();
        }
        return logger;
    }
}

namespace simnet
{
    void initialize_telemetry(TelemetryConfig const& config)
    {
        auto sinks = std::vector<spdlog::sink_ptr> {};

        // Build the active sink set from runtime config.
        if (config.console_log_enabled) {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        if (config.file_log_enabled) {
            std::filesystem::create_directories(config.log_directory);
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                std::filesystem::path { config.log_directory } / "simnet.log",
                true
            ));
        }

        if (sinks.empty()) {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        auto created = std::make_shared<spdlog::logger>("simnet", sinks.begin(), sinks.end());
        created->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
        created->set_level(to_spdlog_level(parse_log_level(config.min_level)));

        std::scoped_lock lock { logger_mutex };
        logger = std::move(created);
    }

    void shutdown_telemetry()
    {
        std::shared_ptr<spdlog::logger> old_logger;
        {
            std::scoped_lock lock { logger_mutex };
            // Moving out makes repeated shutdown calls harmless.
            old_logger = std::move(logger);
            logger.reset();
        }
        if (old_logger) {
            old_logger->flush();
        }
    }

    void log(LogCategory category, LogLevel level, std::string_view message)
    {
        auto active_logger = current_logger();
        active_logger->log(to_spdlog_level(level), "[{}] {}", category_name(category), message);
    }

    void flush_telemetry()
    {
        current_logger()->flush();
    }
}

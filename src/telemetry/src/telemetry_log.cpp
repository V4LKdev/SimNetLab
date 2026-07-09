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
    /// Tracks whether the logger has been initialized, is active, or has been shut down.
    enum class LoggerLifecycle
    {
        NeverInitialized,
        Active,
        Shutdown
    };

    /// Protects the shared logger pointer and its lifecycle state.
    std::mutex logger_mutex;
    std::shared_ptr<spdlog::logger> logger;
    LoggerLifecycle logger_lifecycle { LoggerLifecycle::NeverInitialized };

    // Brings simnet enums into scope
    using namespace simnet;

    /// Converts a simnet LogLevel to the corresponding spdlog level.
    [[nodiscard]] spdlog::level::level_enum to_spdlog_level(LogLevel level) noexcept
    {
        switch (level) {
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

    /// Maps a log category to a short name used in the log output.
    [[nodiscard]] std::string_view category_name(LogCategory category) noexcept
    {
        switch (category) {
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

    /// Create a default logger that outputs to stdout, suitable for early or unconfigured logging.
    [[nodiscard]] std::shared_ptr<spdlog::logger> make_default_logger()
    {
        auto created = spdlog::stdout_color_mt("simnet");
        created->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
        created->set_level(spdlog::level::info);
        return created;
    }

    /// Returns a shared logger pointer, or creates a default one.
    [[nodiscard]] std::shared_ptr<spdlog::logger> current_logger()
    {
        std::scoped_lock lock { logger_mutex };
        if (logger_lifecycle == LoggerLifecycle::Shutdown) {
            // One-way latch: never resurrect after shutdown. Rebuilding sinks during
            // teardown risks use-after-destruction of spdlog/static state. Drop the log.
            return {};
        }
        if (!logger) {
            // Logging before initialization should still be visible
            logger = make_default_logger();
        }
        return logger;
    }
}

// --- Public API ---

namespace simnet
{
    void initialize_telemetry(TelemetryConfig const& config)
    {
        auto sinks = std::vector<spdlog::sink_ptr> {};

        // Build the active sink set from runtime config
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

        // Always keep at least one sink to log calls don't silently fail
        if (sinks.empty()) {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        auto created = std::make_shared<spdlog::logger>("simnet", sinks.begin(), sinks.end());
        // e.g. [12:34:56.789] [info] [simulation] Tick 1234 completed in 10.5ms
        created->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
        created->set_level(to_spdlog_level(parse_log_level(config.min_level)));

        // Swap in the new logger and mark it as active
        std::scoped_lock lock { logger_mutex };
        logger = std::move(created);
        logger_lifecycle = LoggerLifecycle::Active;
    }

    void shutdown_telemetry()
    {
        std::shared_ptr<spdlog::logger> old_logger;
        {
            std::scoped_lock lock { logger_mutex };
            // Moving out makes repeated shutdown calls harmless
            old_logger = std::move(logger);
            logger.reset();
            logger_lifecycle = LoggerLifecycle::Shutdown;
        }
        // Flush after releasing lock to avoid deadlock
        if (old_logger) {
            old_logger->flush();
        }
    }

    void log(LogCategory category, LogLevel level, std::string_view message)
    {
        auto active_logger = current_logger();
        if (!active_logger) {
            return;
        }
        active_logger->log(to_spdlog_level(level), "[{}] {}", category_name(category), message);
    }

    void flush_telemetry()
    {
        auto active_logger = current_logger();
        if (active_logger) {
            active_logger->flush();
        }
    }
}

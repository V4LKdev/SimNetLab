module;

#include <string_view>

/// @brief Telemetry logging lifecycle and sink API.
export module simnet.telemetry:log;

import :types;
import simnet.config;

export namespace simnet
{
    /// Replaces the configured logging sinks. Zero configured sinks disable logging.
    void initialize_telemetry(TelemetryConfig const& config);

    /// Shuts down all sinks. Flushes pending messages before release. Idempotent.
    void shutdown_telemetry();

    /// Writes a single log message with the given category and severity.
    void log(LogCategory category, LogLevel level, std::string_view message);

    /// Reports whether the configured logger can consume this severity.
    [[nodiscard]] bool log_enabled(LogLevel level);
}

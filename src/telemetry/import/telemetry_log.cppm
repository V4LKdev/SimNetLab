module;

#include <string_view>

/// @brief Telemetry logging lifecycle and sink API.
export module simnet.telemetry:log;

import :types;
import simnet.config;

export namespace simnet
{
    /// Initializes the logger, profiler (if enabled), and metric sinks.
    void initialize_telemetry(TelemetryConfig const& config);

    /// Shuts down all sinks. Flushes pending messages before release. Idempotent.
    void shutdown_telemetry();

    /// Writes a single log message with the given category and severity.
    void log(LogCategory category, LogLevel level, std::string_view message);

    /// Flushes any buffered log messages to be written immediately.
    void flush_telemetry();
}

module;

#include <string_view>

/// @brief Telemetry logging lifecycle and sink API.
export module simnet.telemetry:log;

import :types;
import simnet.config;

export namespace simnet
{
    /// Initializes logging, profiling, and metric sinks.
    void initialize_telemetry(TelemetryConfig const& config);

    /// Shuts down telemetry sinks.
    void shutdown_telemetry();

    /// Writes a log message.
    void log(LogCategory category, LogLevel level, std::string_view message);

    /// Flushes telemetry sinks.
    void flush_telemetry();
}

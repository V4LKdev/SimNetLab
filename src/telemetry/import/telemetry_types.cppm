module;

#include <cstdint>
#include <string_view>

/// @brief Telemetry public data contracts.
export module simnet.telemetry:types;

import simnet.core;

export namespace simnet
{
    /// Logging severity.
    enum class LogLevel
    {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Critical,
        Off
    };

    /// Logging source category.
    enum class LogCategory
    {
        Core,
        Config,
        Telemetry,
        Simulation,
        Snapshot,
        Spatial,
        Pipeline,
        Transport,
        Render,
        Benchmark
    };

    /// Per-tick raw telemetry counters.
    struct TickMetrics
    {
        Tick tick {};
        double tick_cpu_ms {};
        double sim_cpu_ms {};
        double snapshot_cpu_ms {};
        double pipeline_cpu_ms {};
        double transport_cpu_ms {};
        double render_cpu_ms {};
        std::uint32_t entity_count {};
        std::uint32_t peer_count {};
        std::uint64_t bytes_sent {};
        std::uint64_t bytes_received {};
        std::uint32_t packets_sent {};
        std::uint32_t packets_received {};
    };

    /// Parses a text log level, defaulting to Info for unknown input.
    [[nodiscard]] LogLevel parse_log_level(std::string_view value);
}

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
        Trace,     /// Most verbose.
        Debug,     /// Detailed diagnostics.
        Info,      /// Normal operational messages.
        Warn,      /// Potential issues.
        Error,     /// Recoverable errors.
        Critical,  /// Unrecoverable failures.
        Off        /// No logging.
    };

    /// Logging source category (by module).
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
        Tick tick {};                     /// Simulation tick.
        double tick_cpu_ms {};            /// CPU time for the full tick.
        double sim_cpu_ms {};             /// CPU time for simulation.
        double snapshot_cpu_ms {};        /// CPU time for snapshot.
        double pipeline_cpu_ms {};        /// CPU time for pipeline.
        double transport_cpu_ms {};       /// CPU time for transport.
        double render_cpu_ms {};          /// CPU time for render.
        std::uint32_t entity_count {};    /// Number of active entities.
        std::uint32_t peer_count {};      /// Number of connected peers.
        std::uint64_t bytes_sent {};      /// Total bytes sent this tick.
        std::uint64_t bytes_received {};  /// Total bytes received this tick.
        std::uint32_t packets_sent {};    /// Total packets sent this tick.
        std::uint32_t packets_received {};/// Total packets received this tick.
    };

    /// Parses a text log level (from config)
    /// @return The corresponding LogLevel, or LogLevel::Info for unknown input.
    [[nodiscard]] LogLevel parse_log_level(std::string_view value);

    /// Returns a Tracy-friendly RGBA color for a log category. Use with SIMNET_TRACE_SCOPE_C.
    [[nodiscard]] constexpr std::uint32_t category_trace_color(LogCategory category) noexcept
    {
        // Yes, I used a color palette for this, programmers are artists too!!!
        switch (category) {
            case LogCategory::Core:       return 0xAAAAAAFF; // mid-grey
            case LogCategory::Config:     return 0xEDC948FF; // muted yellow
            case LogCategory::Telemetry:  return 0x76B7B2FF; // teal
            case LogCategory::Simulation: return 0xF28E2BFF; // orange
            case LogCategory::Snapshot:   return 0x4E79A7FF; // blue
            case LogCategory::Spatial:    return 0xE15759FF; // red
            case LogCategory::Pipeline:   return 0xB07AA1FF; // purple
            case LogCategory::Transport:  return 0x59A14FFF; // green
            case LogCategory::Render:     return 0xFF9DA7FF; // soft pink
            case LogCategory::Benchmark:  return 0x2F4B7CFF; // dark navy
        }
        return 0xFFFFFFFF; // white fallback
    }
}

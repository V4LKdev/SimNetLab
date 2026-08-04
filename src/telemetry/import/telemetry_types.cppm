module;

#include <cstdint>

/// @brief Telemetry public data contracts.
export module simnet.telemetry:types;

export namespace simnet
{
    /// Logging severity.
    enum class LogLevel : std::uint8_t
    {
        Trace, /// Most verbose.
        Debug, /// Detailed diagnostics.
        Info, /// Normal operational messages.
        Warn, /// Potential issues.
        Error, /// Recoverable errors.
        Critical, /// Unrecoverable failures.
        Off /// No logging.
    };

    /// Logging source category (by module).
    enum class LogCategory : std::uint8_t
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

    /// Returns a Tracy-friendly RGBA color for a log category. Use with SIMNET_TRACE_SCOPE_C.
    [[nodiscard]] constexpr std::uint32_t category_trace_color(LogCategory category) noexcept
    {
        // Yes, I used a color palette for this, programmers are artists too!!!
        switch (category) {
            case LogCategory::Core:
                return 0xAAAAAAFF; // mid-gray
            case LogCategory::Config:
                return 0xEDC948FF; // muted yellow
            case LogCategory::Telemetry:
                return 0x76B7B2FF; // teal
            case LogCategory::Simulation:
                return 0xF28E2BFF; // orange
            case LogCategory::Snapshot:
                return 0x4E79A7FF; // blue
            case LogCategory::Spatial:
                return 0xE15759FF; // red
            case LogCategory::Pipeline:
                return 0xB07AA1FF; // purple
            case LogCategory::Transport:
                return 0x59A14FFF; // green
            case LogCategory::Render:
                return 0xFF9DA7FF; // soft pink
            case LogCategory::Benchmark:
                return 0x2F4B7CFF; // dark navy
        }
        return 0xFFFFFFFF; // white fallback
    }
}

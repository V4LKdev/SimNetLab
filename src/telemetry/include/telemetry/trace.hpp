#pragma once

#include <cstdint>

namespace simnet::telemetry::trace_color {
    /// Category color for simulation and server tick work.
    inline constexpr std::uint32_t simulation = 0xE74C3C;
    /// Category color for visualization and rendering work.
    inline constexpr std::uint32_t rendering = 0x2ECC71;
    /// Category color for future transport and packet processing work.
    inline constexpr std::uint32_t networking = 0x3498DB;
    /// Category color for future snapshot pipeline work.
    inline constexpr std::uint32_t pipeline = 0x9B59B6;
    /// Category color for telemetry collection and export work.
    inline constexpr std::uint32_t telemetry = 0xF1C40F;
    /// Category color for file, config, and runtime I/O work.
    inline constexpr std::uint32_t io = 0x95A5A6;
}

#if defined(SIMNET_TELEMETRY_TRACY)
#  include <tracy/Tracy.hpp>
#  define SIMNET_TRACE_ZONE(name) ZoneScopedN(name)
#  define SIMNET_TRACE_ZONE_C(name, color) ZoneScopedNC(name, color)
#  define SIMNET_TRACE_PLOT(name, value) TracyPlot((name), static_cast<double>(value))
#  define SIMNET_TRACE_FRAME(name) FrameMarkNamed(name)
#else
#  define SIMNET_TRACE_ZONE(name) ((void)0)
#  define SIMNET_TRACE_ZONE_C(name, color) ((void)0)
#  define SIMNET_TRACE_PLOT(name, value) ((void)0)
#  define SIMNET_TRACE_FRAME(name) ((void)0)
#endif

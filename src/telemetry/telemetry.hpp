#pragma once

#include <cstdint>
#include <string_view>

// -----------------------------------------------------------------------
// Master switch - if TELEMETRY_ENABLED is not defined, all macros become no-ops.
// -----------------------------------------------------------------------
#ifdef TELEMETRY_ENABLED

#include "timer.hpp"
#include "logger.hpp"

namespace simnet::telemetry {
    void init(std::string_view app_name, std::string_view log_file_path = "simnet_telemetry.log");

    void shutdown();
} // namespace simnet::telemetry

// Convenience macros

// Scoped high‑res timer
#define TELEM_SCOPED_TIMER(name) \
    ::simnet::telemetry::ScopedTimer TELEM_CONCAT(_telem_timer_, __LINE__)(name)

// Frame markers
#define TELEM_FRAME_BEGIN() ::simnet::telemetry::frame_marker(::simnet::telemetry::FrameEvent::Begin)
#define TELEM_FRAME_END()   ::simnet::telemetry::frame_marker(::simnet::telemetry::FrameEvent::End)

// Validation checks
#define TELEM_VALIDATE(condition, message) \
    ::simnet::telemetry::validate(condition, message, __FILE__, __LINE__)

// Logging macros
#define TELEM_LOG_TRACE(...)    SPDLOG_LOGGER_TRACE(::simnet::telemetry::get_logger(), __VA_ARGS__)
#define TELEM_LOG_DEBUG(...)    SPDLOG_LOGGER_DEBUG(::simnet::telemetry::get_logger(), __VA_ARGS__)
#define TELEM_LOG_INFO(...)     SPDLOG_LOGGER_INFO(::simnet::telemetry::get_logger(), __VA_ARGS__)
#define TELEM_LOG_WARN(...)     SPDLOG_LOGGER_WARN(::simnet::telemetry::get_logger(), __VA_ARGS__)
#define TELEM_LOG_ERROR(...)    SPDLOG_LOGGER_ERROR(::simnet::telemetry::get_logger(), __VA_ARGS__)

// Tracy zone
#if TRACY_ENABLED
#define TELEM_TRACY_ZONE(name) ZoneScopedN(name)
#else
#define TELEM_TRACY_ZONE(name)
#endif

#else  // TELEMETRY_ENABLED not defined

#define TELEM_SCOPED_TIMER(name)
#define TELEM_FRAME_BEGIN()
#define TELEM_FRAME_END()
#define TELEM_VALIDATE(condition, message)
#define TELEM_LOG_TRACE(...)
#define TELEM_LOG_DEBUG(...)
#define TELEM_LOG_INFO(...)
#define TELEM_LOG_WARN(...)
#define TELEM_LOG_ERROR(...)
#define TELEM_TRACY_ZONE(name)

#endif  // TELEMETRY_ENABLED

// Helper for concatenating names
#define TELEM_CONCAT_IMPL(x, y) x##y
#define TELEM_CONCAT(x, y) TELEM_CONCAT_IMPL(x, y)

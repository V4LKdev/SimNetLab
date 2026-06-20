#pragma once

#include <string_view>

// =============================================================================
// Master switch – set by CMake when BUILD_TELEMETRY=ON
// =============================================================================

#ifdef TELEMETRY_ENABLED

// ------------------------------------------------------
// Enabled build: include the real logging / tracing deps
// ------------------------------------------------------

#include <spdlog/spdlog.h>          // actual logger
#include <tracy/Tracy.hpp>           // zone macros, plots, etc.

#include "logger.hpp"                // get_logger() declaration
#include "colors.hpp"                // TELEM_COLOR_* constants
#include "metrics_collector.hpp"     // MetricsCollector singleton

namespace simnet::telemetry {
    std::shared_ptr<spdlog::logger> get_logger();

    void init(std::string_view app_name,
              std::string_view log_file_path = "simnet_telemetry.log");

    void shutdown();
}


// =============================================================================
// Tracy zones & frame markers
// =============================================================================

#define TELEM_TRACY_ZONE(name)           ZoneScopedN(name)
#define TELEM_TRACY_ZONE_C(name, color)  ZoneScopedNC(name, color)

// Runtime string zone (the string must remain alive for the zone's lifetime)
#define TELEM_TRACY_ZONE_DYN(name, color)                                    \
    const tracy::SourceLocationData TELEM_CONCAT(__tracy_srcloc_, __LINE__){ \
        name, nullptr, nullptr, 0, color                                     \
    };                                                                       \
    tracy::ScopedZone TELEM_CONCAT(__tracy_zone_, __LINE__)(                 \
        &TELEM_CONCAT(__tracy_srcloc_, __LINE__))

#define TELEM_CONCAT_IMPL(x, y) x##y
#define TELEM_CONCAT(x, y)      TELEM_CONCAT_IMPL(x, y)

#define TELEM_TRACY_PLOT(name, val)   TracyPlot(name, val)
#define TELEM_TRACY_FRAME(name)       FrameMarkNamed(name)
#define TELEM_TRACY_MESSAGE(txt, sz)  TracyMessage(txt, sz)
#define TELEM_TRACY_ALLOC(ptr, sz)    TracyAlloc(ptr, sz)
#define TELEM_TRACY_FREE(ptr)         TracyFree(ptr)


// =============================================================================
// Logging macros (spdlog‑based)
// =============================================================================

#define TELEM_LOG_TRACE(...)                                      \
    do { if (auto _log = ::simnet::telemetry::get_logger())       \
             _log->trace(__VA_ARGS__); } while(0)

#define TELEM_LOG_DEBUG(...)                                      \
    do { if (auto _log = ::simnet::telemetry::get_logger())       \
             _log->debug(__VA_ARGS__); } while(0)

#define TELEM_LOG_INFO(...)                                       \
    do { if (auto _log = ::simnet::telemetry::get_logger())       \
             _log->info(__VA_ARGS__); } while(0)

#define TELEM_LOG_WARN(...)                                       \
    do { if (auto _log = ::simnet::telemetry::get_logger())       \
             _log->warn(__VA_ARGS__); } while(0)

#define TELEM_LOG_ERROR(...)                                      \
    do { if (auto _log = ::simnet::telemetry::get_logger())       \
             _log->error(__VA_ARGS__); } while(0)


// =============================================================================
// Metrics (optional counter / histogram collection)
// =============================================================================

#define TELEM_COUNTER_INC(name, delta)                                   \
    do {                                                                 \
        ::simnet::telemetry::MetricsCollector::instance().add_counter(   \
            name, static_cast<int64_t>(delta));                          \
        TELEM_TRACY_PLOT(name, static_cast<int64_t>(delta));            \
    } while(0)

#define TELEM_COUNTER_ADD(name, value)                                   \
    do { ::simnet::telemetry::MetricsCollector::instance().add_counter(  \
             name, (value)); } while(0)

#define TELEM_COUNTER_SET(name, value)                                   \
    do { ::simnet::telemetry::MetricsCollector::instance().set_counter(  \
             name, (value)); } while(0)

#define TELEM_HISTOGRAM_ADD(name, value)                                 \
    do { ::simnet::telemetry::MetricsCollector::instance().add_histogram(\
             name, static_cast<double>(value)); } while(0)

#define TELEM_FLUSH_METRICS()                                            \
    do { ::simnet::telemetry::MetricsCollector::instance().flush_to_log(); } while(0)


// =============================================================================
// Disabled build: all macros become empty
// =============================================================================
#else

#define TELEM_TRACY_ZONE(name)
#define TELEM_TRACY_ZONE_C(name, color)
#define TELEM_TRACY_ZONE_DYN(name, color)
#define TELEM_TRACY_PLOT(name, val)
#define TELEM_TRACY_FRAME(name)
#define TELEM_TRACY_MESSAGE(txt, sz)
#define TELEM_TRACY_ALLOC(ptr, sz)
#define TELEM_TRACY_FREE(ptr)
#define TELEM_LOG_TRACE(...)
#define TELEM_LOG_DEBUG(...)
#define TELEM_LOG_INFO(...)
#define TELEM_LOG_WARN(...)
#define TELEM_LOG_ERROR(...)
#define TELEM_COUNTER_INC(name, delta)
#define TELEM_COUNTER_ADD(name, value)
#define TELEM_COUNTER_SET(name, value)
#define TELEM_HISTOGRAM_ADD(name, value)
#define TELEM_FLUSH_METRICS()

#endif   // TELEMETRY_ENABLED

#pragma once

#include <string_view>

// -----------------------------------------------------------------------
// Master switch
// -----------------------------------------------------------------------
#ifdef TELEMETRY_ENABLED

#include <spdlog/spdlog.h>

#ifdef TRACY_ENABLED
#include <tracy/Tracy.hpp>
#endif

#include "logger.hpp"
#include "colors.hpp"

namespace simnet::telemetry {
        void init(std::string_view app_name, std::string_view log_file_path = "simnet_telemetry.log");

        void shutdown();
}

// --------------- Tracy zones and frame marking ---------------
#ifdef TRACY_ENABLED
// High-res CPU zone, automatically ends at scope exit
#define TELEM_TRACY_ZONE(name)          ZoneScopedN(name)
#define TELEM_TRACY_ZONE_C(name, color) ZoneScopedNC(name, color)
// Push numeric value into named plot on the timeline
#define TELEM_TRACY_PLOT(name, val)     TracyPlot(name, val)
// Mark the end of a logical frame
#define TELEM_TRACY_FRAME(name)         FrameMarkNamed(name)
// Send log message inline into the trace
#define TELEM_TRACY_MESSAGE(txt, size)   TracyMessage(txt, size)
// Track an aligned memory allocation
#define TELEM_TRACY_ALLOC(ptr, size)    TracyAlloc(ptr, size)
// Track freeing of memory
#define TELEM_TRACY_FREE(ptr)           TracyFree(ptr)
#else
#define TELEM_TRACY_ZONE(name)
#define TELEM_TRACY_PLOT(name, val)
#define TELEM_TRACY_FRAME(name)
#define TELEM_TRACY_MESSAGE(txt, size)
#define TELEM_TRACY_ALLOC(ptr, size)
#define TELEM_TRACY_FREE(ptr)
#endif

// --------------- Logging ---------------
#define TELEM_LOG_TRACE(...)                                            \
    do { if (auto _log = ::simnet::telemetry::get_logger())              \
            _log->trace(__VA_ARGS__); } while(0)

#define TELEM_LOG_DEBUG(...)                                            \
    do { if (auto _log = ::simnet::telemetry::get_logger())              \
            _log->debug(__VA_ARGS__); } while(0)

#define TELEM_LOG_INFO(...)                                             \
    do { if (auto _log = ::simnet::telemetry::get_logger())              \
            _log->info(__VA_ARGS__); } while(0)

#define TELEM_LOG_WARN(...)                                             \
    do { if (auto _log = ::simnet::telemetry::get_logger())              \
            _log->warn(__VA_ARGS__); } while(0)

#define TELEM_LOG_ERROR(...)                                            \
    do { if (auto _log = ::simnet::telemetry::get_logger())              \
            _log->error(__VA_ARGS__); } while(0)

#else  // TELEMETRY_ENABLED not defined

#define TELEM_TRACY_ZONE(name)
#define TELEM_TRACY_PLOT(name, val)
#define TELEM_TRACY_FRAME(name)
#define TELEM_LOG_TRACE(...)
#define TELEM_LOG_DEBUG(...)
#define TELEM_LOG_INFO(...)
#define TELEM_LOG_WARN(...)
#define TELEM_LOG_ERROR(...)

#endif

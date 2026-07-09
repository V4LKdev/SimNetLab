#pragma once

/**
 * @brief Tracy scoped‑trace macros.
 *
 * These macros are active only when the CMake option SIMNET_ENABLE_TRACY
 * is ON. Otherwise they expand to nothing and generate zero runtime cost.
 */
#if defined(SIMNET_ENABLE_TRACY)
#include <tracy/Tracy.hpp>

#define SIMNET_TRACE_SCOPE(name) ZoneScopedN(name)
#define SIMNET_TRACE_SCOPE_C(name, color) ZoneScopedNC(name, color)
#define SIMNET_TRACE_PLOT(name, value) TracyPlot(name, value)
#define SIMNET_TRACE_FRAME(name) FrameMarkNamed(name)
#else
#define SIMNET_TRACE_SCOPE(name) ((void)0)
#define SIMNET_TRACE_SCOPE_C(name, color) ((void)0)
#define SIMNET_TRACE_PLOT(name, value) ((void)0)
#define SIMNET_TRACE_FRAME(name) ((void)0)
#endif

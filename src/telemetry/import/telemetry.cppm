/**
* @defgroup telemetry simnet.telemetry
 * @brief Logging, tracing, and raw metrics.
 *
 * @details
 * Provides a configurable logging sink, thread-safe metric buffers,
 * Tracy profiler integration (when `SIMNET_ENABLE_TRACY` is defined),
 * and a color mapping for trace scopes.
 *
 * @note All public API functions in the log and metrics partitions are
 * thread-safe. Initialization and shutdown must be serialized externally.
 */
export module simnet.telemetry;

/**
 * @par simnet.telemetry:types
 * Core telemetry types: `LogLevel`, `LogCategory`, `TickMetrics`,
 * and `parse_log_level`. Also includes `category_trace_color` for
 * Tracy-compatible color mapping.
 */
export import :types;

/**
 * @par simnet.telemetry:log
 * Logging lifecycle: `initialize_telemetry`, `shutdown_telemetry`,
 * `log`, and `flush_telemetry`.
 */
export import :log;

/**
 * @par simnet.telemetry:metrics
 * Metric submission and retrieval: `TickMetrics`, `MetricRecord`,
 * and associated `submit_*` / `take_*` / `clear_*` functions.
 */
export import :metrics;

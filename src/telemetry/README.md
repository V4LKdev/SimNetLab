@defgroup telemetry simnet.telemetry
@brief Logging, tracing, and raw metrics.

## Exported Types

### simnet.telemetry:types
- `LogLevel` - severity levels: Trace, Debug, Info, Warn, Error, Critical, Off.
- `LogCategory` - source categories: Core, Config, Telemetry, Simulation, Snapshot, Spatial, Pipeline, Transport, Render, Benchmark.
- `TickMetrics` - per‑tick counters: CPU times, entity/peer counts, network bytes and packets.
- `parse_log_level` - converts a configuration string to the corresponding `LogLevel` (defaults to `Info`).
- `category_trace_color` - returns a Tracy‑compatible RGBA color for each log category.

### simnet.telemetry:log
- `initialize_telemetry` - sets up logging sinks and, if enabled, the Tracy profiler. Requires a `TelemetryConfig`.
- `shutdown_telemetry` - flushes and releases all sinks. Safe to call multiple times.
- `log` - writes a message to the active logger. Creates a default stdout logger if called before initialisation.
- `flush_telemetry` - forces any buffered log output to be written immediately.

### simnet.telemetry:metrics
- `MetricValue` - variant type storing one of: `int64`, `uint64`, `double`, `bool`, or `std::string`.
- `MetricField` - a named metric value.
- `MetricRecord` - a stream name, optional tick, and a set of `MetricField` entries.
- `submit_tick_metrics` / `take_tick_metrics` / `clear_tick_metrics` - buffers for per‑tick raw counters.
- `submit_metric_record` / `take_metric_records` / `clear_metric_records` - buffers for generic structured metrics.
- `format_metric_record_key_value` - formats a record as a single line of `stream tick=... field=value...`.

## Trace Macros

Include `<simnet/telemetry_trace.hpp>` to use these macros. They expand to no‑ops unless the build system sets `SIMNET_ENABLE_TRACY=1` and links `Tracy::TracyClient`.

- `SIMNET_TRACE_SCOPE(name)` - scoped profiling zone.
- `SIMNET_TRACE_SCOPE_C(name, color)` - scoped zone with a user‑defined RGBA colour.
- `SIMNET_TRACE_PLOT(name, value)` - plots a value on a timeline.
- `SIMNET_TRACE_FRAME(name)` - marks a frame boundary.

Use `category_trace_color(...)` to obtain the recommended color for a `LogCategory` when calling `SIMNET_TRACE_SCOPE_C`.

## Notes

- All public API functions in the `log` and `metrics` partitions are thread‑safe. Initialization and shutdown must be serialized externally.
- `TickMetrics` and `MetricRecord` are stored in heap‑allocated vectors. Submission functions (`submit_*`) take constant time. retrieval functions (`take_*`) allocate new vectors for the results.
- `log()` always has a valid spdlog logger. If telemetry is never initialised, a default console logger is used automatically.
- `parse_log_level` performs case‑insensitive comparison. Unrecognized strings map to `LogLevel::Info`.
- The color palette returned by `category_trace_color` is based on Tableau 10 and is tuned for distinctness in the profiler.

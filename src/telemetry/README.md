@defgroup telemetry simnet.telemetry
@brief Logging, tracing, and typed runtime measurements.

## Exported Types

### simnet.telemetry:types
- `LogLevel` - severity levels: Trace, Debug, Info, Warn, Error, Critical, Off.
- `LogCategory` - source categories: Core, Config, Telemetry, Simulation, Snapshot, Spatial, Pipeline, Transport, Render, Benchmark.
- `TickMetrics` - legacy unused per-tick counters retained for TEL-002 cleanup.
- `parse_log_level` - converts a configuration string to the corresponding `LogLevel` (defaults to `Info`).
- `category_trace_color` - returns a Tracy-compatible RGBA color for each log category.

### simnet.telemetry:log
- `initialize_telemetry` - sets up logging sinks. Requires a `TelemetryConfig`.
- `shutdown_telemetry` - flushes and releases all sinks. Safe to call multiple times.
- `log` - writes a message to the active logger. Creates a default stdout logger if called before initialization.
- `flush_telemetry` - forces any buffered log output to be written immediately.

### simnet.telemetry:metrics
- `ServerReplicationMeasurement` - one Server replication attempt with application-owned stage
  durations and explicit byte ownership.
- `ClientReplicationMeasurement` - one Client Snapshot-lane attempt from decode through canonical
  snapshot commit and the nonauthoritative Flecs sink.
- `ServerReplicationMeasurements` / `ClientReplicationMeasurements` - allocation-free latest
  record and completion counts used by the current application runtimes.
- `MetricValue` - variant type storing one of: `int64`, `uint64`, `double`, `bool`, or `std::string`.
- `MetricField` - a named metric value.
- `MetricRecord` - a stream name, optional tick, and a set of `MetricField` entries.
- `submit_tick_metrics` / `take_tick_metrics` / `clear_tick_metrics` - buffers for per-tick raw counters.
- `submit_metric_record` / `take_metric_records` / `clear_metric_records` - buffers for generic structured metrics.
- `format_metric_record_key_value` - formats a record as a single line of `stream tick=... field=value...`.

The application runtimes own all timing boundaries. The telemetry module owns only value-like
records and the allocation-free current observers. Snapshot, pipeline, transport, game, and render
reports remain domain results. They do not write research output.

The Server records snapshot extraction, baseline resolution, encoding, transport submission, and
retained-baseline storage. `encoded_update_bytes` is the complete pipeline update.
`application_payload_bytes` is the update offered to transport. `transport_payload_bytes` is the
application payload accepted by transport and excludes network protocol overhead.

The Client records decode, retained-baseline resolution, reconstruction, sink preparation, Client
Flecs sink application, canonical snapshot commit, and total receive-to-applied CPU work. Retained
reconstructed snapshots remain canonical Client state. Pipeline-only treatments exclude sink
preparation and sink application. Full-system treatments include both and report sink application
separately. A failed attempt never enters `latest_applied` and never increments `applied_count`.

The current runtime consumer keeps only the latest attempt, latest successful result, and counts,
then logs the latest record during shutdown. TEL-003 owns buffered raw CSV rows, stable schemas,
headers, run identity, timestamps, flushing, and file lifecycle. No in-process field represents
externally captured packet bytes, operating-system counters, perf data, energy data, or netem data.

## Trace Macros

Include `<simnet/telemetry_trace.hpp>` to use these macros. They expand to no-ops unless the build system sets `SIMNET_ENABLE_TRACY=1` and links `Tracy::TracyClient`.

- `SIMNET_TRACE_SCOPE(name)` - scoped profiling zone.
- `SIMNET_TRACE_SCOPE_C(name, color)` - scoped zone with a user-defined RGBA color.
- `SIMNET_TRACE_PLOT(name, value)` - plots a value on a timeline.
- `SIMNET_TRACE_FRAME(name)` - marks a frame boundary.

Use `category_trace_color(...)` to obtain the recommended color for a `LogCategory` when calling `SIMNET_TRACE_SCOPE_C`.

Tracy instrumentation is controlled by the CMake `SIMNET_ENABLE_TRACY` option. It is not a JSON runtime setting. Capture while the process is running. Server and Client log whether their executable includes Tracy instrumentation during startup.

## Notes

- Logging and legacy metric-buffer functions are thread-safe. Initialization and shutdown must be
  serialized externally. Replication observers are application-owned and are not synchronized.
- `TickMetrics` and `MetricRecord` are unused legacy APIs. Their heap-backed buffers and formatting
  path remain only until TEL-002 removes the general telemetry API.
- Replication measurement observation performs fixed-size value assignment without formatting,
  logging, file I/O, or heap allocation.
- Tracy is an optional diagnostic view. It is not final research evidence.
- `log()` always has a valid spdlog logger. If telemetry is never initialized, a default console logger is used automatically.
- `parse_log_level` performs case-insensitive comparison. Unrecognized strings map to `LogLevel::Info`.
- The color palette returned by `category_trace_color` is based on Tableau 10 and is tuned for distinctness in the profiler.

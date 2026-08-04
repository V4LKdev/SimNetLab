@defgroup telemetry simnet.telemetry
@brief Logging, tracing, and typed runtime measurements.

## Exported Types

### simnet.telemetry:types
- `LogLevel` - severity levels: Trace, Debug, Info, Warn, Error, Critical, Off.
- `LogCategory` - source categories: Core, Config, Telemetry, Simulation, Snapshot, Spatial, Pipeline, Transport, Render, Benchmark.
- `category_trace_color` - returns a Tracy-compatible RGBA color for each log category.

### simnet.telemetry:log
- `initialize_telemetry` - replaces the configured logging sinks. Zero sinks disable logging.
- `shutdown_telemetry` - flushes and releases all sinks. Safe to call multiple times.
- `log` - writes a message when a configured sink accepts its severity.
- `log_enabled` - reports whether a configured sink accepts a severity before callers format text.

### simnet.telemetry:metrics
- `ServerReplicationMeasurement` - one Server replication attempt with application-owned stage
  durations and explicit byte ownership.
- `ClientReplicationMeasurement` - one Client Snapshot-lane attempt from decode through canonical
  snapshot commit and the nonauthoritative Flecs sink.
- `ServerReplicationMeasurements` / `ClientReplicationMeasurements` - allocation-free latest
  record and completion counts used by the current application runtimes.

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

The current runtime consumer keeps the latest attempt, latest successful result, and counts for
the shutdown summary. Each application also submits every attempt to its role-specific v1 CSV
writer after the measured stage ends.

### simnet.telemetry:csv

- `EvidenceRunContext` - immutable process role, process-start clocks, and validated run ID.
- `ServerReplicationCsvWriter` - bounded typed Server rows and the Server v1 schema.
- `ClientReplicationCsvWriter` - bounded typed Client rows and the Client v1 schema.
- `EvidenceCsvFile` - exclusive creation and checked write, flush, and close operations.

Server and Client accept optional `--run-id TEXT`. Supplied values must contain 1 to 64 ASCII
characters and match `[A-Za-z0-9][A-Za-z0-9._-]*`. The value is preserved in CSV fields and is
never used in a path. An omitted value becomes `server-<process_started_unix_ns>` or
`client-<process_started_unix_ns>`. Independently generated defaults do not prove that two
processes belong to the same experiment.

The application captures each record envelope after its TEL-001 measured stage. `record_order` is
the authoritative order within one file. `recorded_at_unix_ns` supports approximate cross-process
alignment. `elapsed_since_process_start_ns` is monotonic within the process. The role and
process-start timestamp identify the producing process. Client rows also store the authoritative
role from `JoinAccepted`.

Replication writers reserve 256 typed records at startup and request a drain at 128. Submission
copies only the measurement and envelope. Formatting and file I/O occur during explicit
application drains outside TEL-001 stage boundaries. Buffer overflow and open, write, flush, or
close failures make evidence collection fail and cause the owning process to fail. Files use
exclusive creation and are never truncated, appended to, or overwritten.

The Server boid evidence path uses the same process envelope, exclusive file lifecycle, buffer
capacity, and drain threshold. It retains its existing sample cadence of one aggregate report per
simulated second plus the last unsampled tick. The boid schema remains separate from replication
because it describes simulation diagnostics and phase timings rather than a replication attempt.

Enabled files are named `server_replication_v1_<process_started_unix_ns>.csv`,
`client_replication_v1_<process_started_unix_ns>.csv`, and
`server_boids_v1_<process_started_unix_ns>.csv`. Semantic column changes require a new schema
version.

No in-process field represents externally captured packet bytes, operating-system counters, perf
data, energy data, or netem data.

## Trace Macros

Include `<simnet/telemetry_trace.hpp>` to use these macros. They expand to no-ops unless the build system sets `SIMNET_ENABLE_TRACY=1` and links `Tracy::TracyClient`.

- `SIMNET_TRACE_SCOPE(name)` - scoped profiling zone.
- `SIMNET_TRACE_SCOPE_C(name, color)` - scoped zone with a user-defined RGBA color.
- `SIMNET_TRACE_PLOT(name, value)` - plots a value on a timeline.
- `SIMNET_TRACE_FRAME(name)` - marks a frame boundary.

Use `category_trace_color(...)` to obtain the recommended color for a `LogCategory` when calling `SIMNET_TRACE_SCOPE_C`.

Tracy instrumentation is controlled by the CMake `SIMNET_ENABLE_TRACY` option. It is not a JSON runtime setting. Capture while the process is running. Server and Client log whether their executable includes Tracy instrumentation during startup.

## Notes

- Logging functions are thread-safe. Initialization and shutdown must be serialized externally.
  Calls before initialization, after shutdown, or under a zero-sink configuration are no-ops.
  Replication observers are application-owned and are not synchronized.
- Replication measurement observation and CSV submission perform fixed-size value assignment
  without formatting, logging, file I/O, or heap allocation after startup reservation.
- Tracy is an optional diagnostic view. It is not final research evidence.
- Log-level configuration remains case-insensitive. Unrecognized values still map to `Info` until
  CFG-001 makes configuration validation fail closed.
- The color palette returned by `category_trace_color` is based on Tableau 10 and is tuned for distinctness in the profiler.

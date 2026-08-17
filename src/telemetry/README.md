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

Server v4 records one row for every replication attempt. Static treatment configuration remains in
the archived JSON and the configuration fingerprints rather than being repeated on every row. The
retained technique evidence covers AOI and LOD populations and scheduling, Delta selection and byte
savings, representation error, compression, recovery, and canonical state identity.

`encoded_update_bytes` is the complete pipeline update including its application header.
`transport_accepted_bytes` is the sum of application payload bytes accepted by the transport API.
`transport_accepted_packet_count` is the number of accepted application packet submissions. These
values do not claim to be physical UDP wire bytes or datagrams. Mean accepted application packet
size is derived by dividing accepted bytes by accepted packet count. `packet_header_bytes` is the
total explicit application packet header size prepared for the update.

`compression_input_bytes` and `compression_output_bytes` bound the complete ordinary Raw or Zstd
transform. `compression_encoding` is `disabled`, `raw`, `zstd`, or `mixed`.
`compression_raw_fallback` is true for whole-update Raw output or when any per-packet transform used
Raw. Whole-update values describe the transform before packetization. Per-packet values are sums
over complete packet transforms, so their input includes the explicit packet headers. Server v4
has no dictionary, compression payload, or compression envelope columns.

The Client records one row for each received Snapshot-lane application packet. In Client v3,
`received_outer_bytes` is the current packet payload size. Per-packet decompression fields describe
only the current packet rather than accumulated group work. Whole-update decompression appears only
on the packet that completes reassembly, while earlier packets contain no decompression observation.
Retained reconstructed snapshots remain canonical Client state. A failed attempt never enters
`latest_applied` and never increments `applied_count`.

Every replication duration column ending in `_elapsed_ns` is elapsed wall time measured with
`std::chrono::steady_clock`. It is not process CPU time. Server v4 retains only encode, compression,
transport submission, and total replication elapsed time. Compression elapsed time covers the
complete successful caller-visible transform, including envelope work, Raw copying, scratch work,
and final output construction. Server total elapsed time starts with per-peer baseline work and ends
after retention commit. Representation quality sampling runs after encoding and is excluded from
both encode and total replication durations. The Client v3 timing contract remains unchanged.

The current runtime consumer keeps the latest attempt, latest successful result, and counts for
the shutdown summary. Each application also submits every attempt to its role-specific CSV
writer after the measured stage ends.

### simnet.telemetry:csv

- `EvidenceRunContext` - immutable process role, process-start clocks, and validated run ID.
- `ServerReplicationCsvWriter` - bounded peer-attributed Server rows and the Server v4 schema.
- `ClientReplicationCsvWriter` - bounded typed Client rows and the Client v3 schema.
- `EvidenceCsvFile` - exclusive creation and checked write, flush, and close operations.

Server and Client accept optional `--run-id TEXT`. Supplied values must contain 1 to 64 ASCII
characters and match `[A-Za-z0-9][A-Za-z0-9._-]*`. The value is preserved in CSV fields and is
never used in a path. An omitted value becomes `server-<process_started_unix_ns>` or
`client-<process_started_unix_ns>`. Independently generated defaults do not prove that two
processes belong to the same experiment.

The application captures each record envelope after its measured replication stage.
`record_order` is the authoritative order within one file. `recorded_at_unix_ns` supports
approximate cross-process alignment. `elapsed_since_process_start_ns` is monotonic within the
process. The role and process-start timestamp identify the producing process. Server rows store
the Server-assigned peer ID and accepted gameplay role. Client rows store the Server-assigned peer
ID and authoritative role from `JoinAccepted`. Runtime configuration fingerprints are role-local
and are not expected to match. Network compatibility and application-wire fingerprints are
expected to match between Server and Client.

Replication writers reserve 256 typed records at startup and request a drain at 128. Submission
copies only the measurement and envelope. Formatting and file I/O occur during explicit
application drains outside replication timing boundaries. Buffer overflow and open, write, flush,
or close failures make evidence collection fail and cause the owning process to fail. Files use
exclusive creation and are never truncated, appended to, or overwritten.

Applications use explicit `close()` calls as the failure-reporting boundary. Destructors perform
only best-effort fallback cleanup and do not report failures.

Enabled files are named `server_replication_v4_<process_started_unix_ns>.csv`,
`client_replication_v3_<process_started_unix_ns>.csv`. Semantic column changes require a new schema
version.

When CSV evidence is disabled, writers create no directory or file. They also skip timestamp
capture and row formatting. Application report flattening remains fixed-size value assignment and
does no file I/O.

No in-process field represents captured network bytes, operating-system counters, perf data,
energy data, or netem data. `scripts/simnet_linux_collector.py` owns the separate Linux process and
host evidence layer.

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
- Logging initialization accepts only `trace`, `debug`, `info`, `warn`, `error`, `critical`, or
  `off`. Invalid values leave the active logger unchanged.
- Replication measurement observation and CSV submission perform fixed-size value assignment
  without formatting, logging, file I/O, or heap allocation after startup reservation.
- Tracy is an optional diagnostic view. It is not final research evidence.
- The color palette returned by `category_trace_color` is based on Tableau 10 and is tuned for distinctness in the profiler.

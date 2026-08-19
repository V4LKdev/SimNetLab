# simnet_telemetry

`simnet_telemetry` provides logging, tracing, typed runtime measurements, and checked CSV persistence.

## Public API

- `simnet.telemetry:types` exports `LogLevel`, `LogCategory`, timing aliases, and `category_trace_color`.
- `simnet.telemetry:log` exports `initialize_telemetry`, `shutdown_telemetry`, `log`, and `log_enabled`.
- `simnet.telemetry:metrics` exports Server and Client measurement records and current observers.
- `simnet.telemetry:csv` exports `EvidenceRunContext`, `EvidenceCsvFile`, and role-specific writers.

Application runtimes own the measurement boundaries. Telemetry owns value-like records, current
observers, logging, and persistence. Snapshot, pipeline, transport, game, and render reports remain
domain results and do not write CSV output.

## Evidence ownership

Server v4 has 68 columns and Client v4 has 31 columns. Static configuration remains in archived JSON
and fingerprints instead of being repeated in every row. The role-specific CSV files contain the
replication measurements. There is no separate boid CSV or dictionary telemetry.

Runtime configuration fingerprints are role-local and are not expected to match across processes.
Network compatibility and application-wire fingerprints must match between a connected Server and
Client. Server rows carry the Server-assigned peer ID and accepted gameplay role. Client rows carry
that peer ID and the authoritative role from `JoinAccepted`.

## Server v4

Server v4 records one row for every replication attempt, including skips and terminal failures. Its
technique fields cover AOI and LOD populations and scheduling, Delta selection and bytes,
representation error, compression, recovery, and canonical state identity.

`encoded_update_bytes` is the complete pipeline update including its application header.
`transport_accepted_bytes` is the application payload accepted by the transport API, and
`transport_accepted_packet_count` counts accepted application packet submissions. Neither field
claims physical wire bytes or UDP datagrams. `packet_header_bytes` is the total explicit application
packet header size prepared for the update.

The runtime observer retains the latest attempt, latest successful result, and summary counts. The
application also submits every attempt to the Server writer after the measured stage ends.

## Client v4

Client v4 records exactly one row for each received Snapshot-lane application packet.
`received_packet_bytes` is the current packet's transport payload size. `packet_group_id` identifies
reassembly, while `sequence` identifies a decoded update.

Before valid update-header inspection, sequence and update fields are zero and `snapshot_kind` is
`not_available`. Functional reassembly expiry emits no CSV row. The `outcome` field records packet
status.

Per-packet decompression fields describe only the current packet. Raw and Zstd packets retain their
own encoding, input, output, and elapsed transform work. Whole-update decompression appears only on
the packet that completes reassembly. Earlier packets use `not_required` with zero decompression
values. Disabled compression uses `disabled` with zero decompression values.

Reconstructed snapshots are canonical Client state. Canonical count and fingerprint are populated
only after successful application and canonical commit.

## Timing and byte boundaries

`compression_input_bytes` and `compression_output_bytes` cover complete ordinary Raw or Zstd
transforms. Whole-update values describe the transform before packetization. Per-packet values are
sums over complete packet transforms, so their input includes explicit packet headers.
`compression_encoding` is `disabled`, `raw`, `zstd`, or `mixed`.
`compression_raw_fallback` is true for whole-update Raw output or when any per-packet transform uses
Raw.

Every replication field ending in `_elapsed_ns` is steady-clock elapsed wall time, not process CPU
time. Compression and decompression timing covers the complete caller-visible transform, including
envelope work, codec or Raw work, scratch storage, and final output construction. A failed Client
decompression attempt reports only work completed for that current packet.

Server v4 retains `encode_elapsed_ns`, `compression_elapsed_ns`,
`transport_submission_elapsed_ns`, and `total_replication_elapsed_ns`. Total replication timing
starts with per-peer baseline work and ends after retention commit. Encode timing wraps pipeline
encoding, while transport submission timing wraps the complete packet-submission loop.
Representation quality sampling runs after encoding and is excluded from encode and total
replication timing.

Client v4 retains `decompression_elapsed_ns`, `decode_elapsed_ns`, and
`decode_to_applied_elapsed_ns`. Decode-to-applied starts with decode and ends after canonical commit,
and is populated only for `applied` rows.

## CSV lifecycle

Server and Client accept optional `--run-id TEXT`. A supplied ID contains 1 to 64 ASCII characters
and matches `[A-Za-z0-9][A-Za-z0-9._-]*`. It is preserved in the rows and never used in a path.
Omitting it creates a process-local `server-<process_started_unix_ns>` or
`client-<process_started_unix_ns>` value, which does not associate independently started processes.

The application captures each record envelope after the measured stage. `record_order` defines file
order. `recorded_at_unix_ns` supports approximate cross-process alignment, while
`elapsed_since_process_start_ns` is monotonic within one process.

Each writer reserves 256 typed records and requests a drain at 128. Submission copies the
measurement and envelope. Formatting and file I/O occur during explicit application drains outside
replication timing. Observation and submission remain fixed-size value assignment after startup
reservation.

Files use exclusive creation and are never truncated, appended to, or overwritten. Buffer overflow
and open, write, flush, or close failures fail evidence collection and the owning process. Explicit
`close()` is the checked failure boundary. Destructors perform only best-effort cleanup.

Enabled filenames are `server_replication_v4_<process_started_unix_ns>.csv` and
`client_replication_v4_<process_started_unix_ns>.csv`. Semantic column changes require a new schema
version. When CSV is disabled, writers create no directory or file and skip timestamp capture and
row formatting.

## Logging

`initialize_telemetry` validates configuration and replaces the active sinks. Zero sinks disable
logging, while initialization failure preserves the current logger. `log_enabled` lets callers
avoid formatting rejected messages. `shutdown_telemetry` flushes and releases sinks and is
idempotent.

Logging calls are thread-safe, but initialization and shutdown must be serialized externally.
Calls before initialization, after shutdown, or with no sinks are no-ops. Accepted level names are
`trace`, `debug`, `info`, `warn`, `error`, `critical`, and `off`. Invalid values leave the active
logger unchanged. Application-owned replication observers are not synchronized.

## Tracy

Include `<simnet/telemetry_trace.hpp>` for `SIMNET_TRACE_SCOPE`, `SIMNET_TRACE_SCOPE_C`,
`SIMNET_TRACE_PLOT`, and `SIMNET_TRACE_FRAME`. They expand to no-ops unless CMake enables
`SIMNET_ENABLE_TRACY=1` and links `Tracy::TracyClient`.

Tracy is a compile-time diagnostic option, not a JSON setting, and is excluded from headless
measurements. Server and Client report its availability at startup. `category_trace_color` supplies
the recommended `LogCategory` color for colored zones.

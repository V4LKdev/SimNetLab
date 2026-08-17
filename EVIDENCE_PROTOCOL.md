# Final evidence protocol

This protocol applies to final thesis evidence collected from the frozen SimNetLab revision. Every
treatment configuration uses 30 complete repetitions. Each repetition contributes one run-level
aggregate per dependent variable. Tick rows and packet rows are observations within a run. They
are never independent ANOVA subjects.

Treatment execution order should be randomized or counterbalanced within repetition blocks. The
actual execution order must be archived.

## Authoritative tick window

- Warm-up ticks are 1 through 600.
- Measured ticks are 601 through 2400 inclusive.
- Ticks 2401 through 2460 provide convergence and shutdown allowance.
- The Server command limit is `--max-ticks 2460`.
- Final run-level aggregates include only authoritative updates attributed to ticks 601 through
  2400.

Reduced-cadence treatments do not produce one Server row per tick. A complete run reaches the
complete authoritative interval and produces every update expected by the configured cadence. An
intentionally skipped cadence tick does not require a Server row with outcome `sent`.

## Run classes

Semantic and whole-process perf evidence come from separate matched executions. Do not combine
their results as if they came from one process execution.

### Semantic run

A semantic run collects primary networking outcomes, technique-specific evidence, canonical
convergence, and coarse application elapsed timings.

- Use `build/evidence` for ordinary workloads.
- Use `build/evidence-synthetic` for the four controlled synthetic Delta workloads.
- Rendering and Tracy must be disabled.
- Set `telemetry.metrics_csv_enabled` to `true`.
- Set `telemetry.console_log_enabled` and `telemetry.file_log_enabled` to `false` in the archived
  Server and Client configurations.
- Use the same logging and telemetry settings for every semantic treatment.
- Use one shared run ID for Server and Client.

### Whole-process perf run

A whole-process perf run collects whole-process `task-clock`, cycles, and instructions.

- Use a separate process execution matched to the semantic treatment.
- Set `telemetry.metrics_csv_enabled`, `telemetry.console_log_enabled`, and
  `telemetry.file_log_enabled` to `false`.
- Keep rendering and Tracy disabled.
- Keep the simulation and networking treatment identical to the matched semantic run.
- Invoke each process directly with `perf stat -- <complete application command>`.
- Archive raw perf output and the exact expanded command.

Application stage elapsed values are elapsed wall time. They are not process CPU time.

## Primary dependent variables

Primary networking outcomes come from successful Server v4 rows in the measured tick interval.

### Submitted application bytes

Use `transport_accepted_bytes`. Include only Server rows with outcome `sent` and a tick from 601
through 2400. Sum the field across the measured interval.

This value is application payload bytes accepted by the transport API. It is not physical UDP wire
bytes or Ethernet bytes.

### Application packet submissions

Use `transport_accepted_packet_count`. Include only Server rows with outcome `sent` and a tick from
601 through 2400. Sum the field across the measured interval.

This value counts accepted application packet submissions. It is not guaranteed to equal the
number of physical UDP datagrams.

### Mean accepted application packet size

For one run, derive:

```text
sum(transport_accepted_bytes) / sum(transport_accepted_packet_count)
```

Use the same measured `sent` rows for both sums. The run is invalid for this derived value when the
denominator is zero.

### Encoded update bytes

Use `encoded_update_bytes` from measured `sent` rows. The final run-level value is the sum over
measured sent updates. Also retain the arithmetic mean per measured sent update as a descriptive
value. An optional descriptive normalization divides the summed bytes by the summed
`selected_entity_count`. Declare any normalization before analysis. Do not use per-row values as
ANOVA subjects.

### Update counts

- Sent update count is the number of measured Server rows with outcome `sent`.
- Applied update count is the number of terminal Client rows with outcome `applied` whose decoded
  tick is in the measured interval.

### Population and workload

Retain run-level summaries of `source_entity_count`, `selected_entity_count`, `upsert_count`,
`delete_count`, and `canonical_entity_count`. State whether each summary is a total, mean, maximum,
or terminal value before analysis.

## Technique-specific derivations

### Representation

Derive run-level mean position error as:

```text
sum(position_error_world_units_sum) / sum(representation_sample_count)
```

Derive run-level mean heading error as:

```text
sum(heading_error_degrees_sum) / sum(representation_sample_count)
```

Use the same measured `sent` rows in each numerator and denominator. Do not average per-row
averages. The run-level maximum position and heading errors are the maxima of
`position_error_world_units_max` and `heading_error_degrees_max` over those rows.

### Delta

Retain measured run-level totals for:

- `delta_unchanged_entity_count`
- `delta_spawned_entity_count`
- `delta_whole_record_entity_count`
- `delta_field_mask_entity_count`
- `delta_classification_field_count`
- `delta_position_field_count`
- `delta_heading_field_count`
- `delta_hue_field_count`
- `delta_complete_record_equivalent_bytes`
- `delta_encoded_record_bytes`

### Compression

For rows where compression was attempted and the summed input denominator is nonzero, derive:

```text
sum(compression_output_bytes) / sum(compression_input_bytes)
```

Retain encoding counts, the `compression_raw_fallback` count or rate, and compression elapsed total
or mean. Retain Client decompression elapsed total or mean using the packet-group attribution rule
below. Compression and decompression fields ending in `_elapsed_ns` are elapsed wall time, not CPU
time.

### Unreliable delivery

Retain recovery reason counts, `recovery_forced_upsert_count`,
`recovery_forced_delete_count`, `repeated_without_ack_upsert_count`,
`repeated_without_ack_delete_count`, the run maximum of `submissions_since_ack_progress`, and final
canonical convergence.

An unreliable treatment without observed or externally introduced loss is only a delivery-policy
comparison. Recovery efficacy may be claimed only when packet impairment actually occurs. Archive
the exact impairment command and host interface. This protocol does not invent a fixed loss rate.

## Client packet grouping rule

Client v4 has one row per received Snapshot-lane application packet. Nonterminal packet rows may
have `tick = 0`, `sequence = 0`, and `snapshot_kind = not_available`.

- Do not select all Client packet rows by their own `tick` field.
- Group Client rows by `packet_group_id`.
- The terminal row establishes the decoded update tick and sequence.
- Attribute every packet row in that completed group to the terminal row's tick and sequence.
- Include the complete group when its terminal tick is from 601 through 2400.
- Exclude the complete group when its terminal tick is outside the measured interval.
- Treat a group originating from the measured interval but lacking a terminal row as an incomplete
  run.
- Do not use incomplete packet rows as independent observations.
- Treat Client packet bytes as diagnostic and validation evidence.
- Derive primary submitted-byte and packet-count outcomes from Server v4.

Server and Client row counts need not match. A Server row represents one replication attempt. A
Client row represents one received Snapshot-lane application packet.

## Fingerprint rules

- Server and Client runtime configuration fingerprints are role-local.
- The Server runtime fingerprint is not expected to equal the Client runtime fingerprint.
- Each runtime fingerprint must match its own archived role-local and shared configuration.
- Network compatibility fingerprints must match across Server and Client.
- Application-wire fingerprints must match across Server and Client.

## Semantic run completeness

A semantic repetition is valid only when every applicable condition below holds.

- One shared run ID is used by Server and Client.
- The expected process roles are present.
- Exact shared, Server, and Client JSON files are archived.
- Server and Client exit normally on the declared runtime limit.
- The Server reaches tick 2460.
- The complete measured interval exists.
- Every configured-cadence emission expected from ticks 601 through 2400 has a terminal Server row.
- No invalid Server terminal outcome enters the measured interval.
- Accepted bytes and packet counts come only from successful transport submissions.
- Server and Client CSV writers remain healthy and close successfully.
- Every Server row has exactly 68 columns.
- Every Client row has exactly 31 columns.
- Network compatibility and application-wire fingerprints match across roles.
- Role-local runtime fingerprints match their archived configurations.
- The final required Client canonical count and fingerprint converge to the sequence-aligned Server
  state before shutdown.
- No fatal runtime condition occurs.
- No dropped-time or overload warning invalidates the measured interval.
- Any impairment command is archived verbatim.

Expected unreliable packet outcomes do not automatically invalidate a run. Failure to converge
does invalidate a run. Apply only these predeclared completeness rules. Do not exclude a run after
inspecting whether its results are convenient.

## Run archive convention

Use one compact directory per repetition:

```text
evidence/<revision>/<study>/<treatment>/rep-01/
  config/
    shared.json
    server-semantic.json
    client-semantic.json
    server-perf.json
    client-perf.json
  semantic/
    server_replication_v4_<process_started_unix_ns>.csv
    client_replication_v4_<process_started_unix_ns>.csv
    server.stdout.txt
    client.stdout.txt
  perf/
    server.csv
    client.csv
    server.stderr.txt
    client.stderr.txt
  provenance/
    commands.txt
    revision.txt
    dirty-state.txt
    build.txt
    compiler.txt
    host.txt
    treatment-order.txt
    impairment.txt
```

The applications directly create only the timestamped v4 semantic CSV files in their configured
`telemetry.log_directory`. The `perf/server.csv` and `perf/client.csv` files are raw output paths
passed to `perf stat`. If canonical semantic archive names are introduced after a run, they are
post-run copies or renames made only after the application writer closes successfully.

## Exact semantic commands

Run Server and Client in separate terminals. Replace `<archive-root>` and `<run-id>` with archived
literal values. The role-local and shared JSON files must contain the semantic telemetry settings
defined above.

Ordinary Server:

```sh
build/evidence/app/Server \
  --config <archive-root>/config/server-semantic.json \
  --shared-config <archive-root>/config/shared.json \
  --run-id <run-id> \
  --max-ticks 2460 \
  > <archive-root>/semantic/server.stdout.txt 2>&1
```

Ordinary Client:

```sh
build/evidence/app/Client \
  --config <archive-root>/config/client-semantic.json \
  --shared-config <archive-root>/config/shared.json \
  --run-id <run-id> \
  --max-ticks 2460 \
  > <archive-root>/semantic/client.stdout.txt 2>&1
```

Synthetic Server:

```sh
build/evidence-synthetic/app/Server \
  --config <archive-root>/config/server-semantic.json \
  --shared-config <archive-root>/config/shared.json \
  --run-id <run-id> \
  --max-ticks 2460 \
  > <archive-root>/semantic/server.stdout.txt 2>&1
```

Synthetic Client:

```sh
build/evidence-synthetic/app/Client \
  --config <archive-root>/config/client-semantic.json \
  --shared-config <archive-root>/config/shared.json \
  --run-id <run-id> \
  --max-ticks 2460 \
  > <archive-root>/semantic/client.stdout.txt 2>&1
```

Do not use a launcher. Archive both expanded commands exactly.

## Direct whole-process perf commands

Run the perf Server and Client commands separately because both processes must run. Use a
perf-specific shared run ID and archived perf configurations with CSV and logging disabled.

Server terminal:

```sh
perf stat --no-big-num -x, \
  -e task-clock,cycles,instructions \
  -o <archive-root>/perf/server.csv \
  -- build/evidence/app/Server \
  --config <archive-root>/config/server-perf.json \
  --shared-config <archive-root>/config/shared.json \
  --run-id <perf-run-id> \
  --max-ticks 2460 \
  2> <archive-root>/perf/server.stderr.txt
```

Client terminal:

```sh
perf stat --no-big-num -x, \
  -e task-clock,cycles,instructions \
  -o <archive-root>/perf/client.csv \
  -- build/evidence/app/Client \
  --config <archive-root>/config/client-perf.json \
  --shared-config <archive-root>/config/shared.json \
  --run-id <perf-run-id> \
  --max-ticks 2460 \
  2> <archive-root>/perf/client.stderr.txt
```

Substitute the synthetic executable paths for synthetic treatments. Raw `perf stat` output is the
authority. Do not convert it into a repository-defined derived schema.

## Provenance

Archive the output of:

```sh
git rev-parse HEAD
git status --porcelain=v1
cmake --version
uname -a
lscpu
getconf _NPROCESSORS_ONLN
hostname
```

Also archive:

- Configure and build preset names.
- Effective CMake feature options.
- Compiler path, name, and version.
- Complete Server and Client commands.
- Server and Client exit statuses.
- Treatment execution order.
- External impairment command and interface when applicable.

An empty `git status --porcelain=v1` output records a clean worktree. Record the compiler selected
by CMake and run that exact executable with `--version`. The deleted Linux collector and perf
wrapper are not part of this procedure.

## Instrumentation-overhead pilot

Before final collection, run five paired control repetitions with semantic CSV enabled and five
matched control repetitions with semantic CSV disabled. Use the same host, binaries, treatment,
and tick limits. Interleave or randomize the enabled and disabled order.

Compare paired whole-process `task-clock` and primary networking outcomes. Report relative
difference as `(enabled - disabled) / disabled` when the denominator is nonzero. Do not infer an
unrecorded value. Keep final semantic and perf evidence separate when instrumentation materially
changes process cost.

## Final dry-run checklist

Complete one successful dry run for each condition before final data collection:

- No compression.
- Whole-update ordinary Zstd.
- Per-packet ordinary Zstd with multiple packets.
- Reliable-sequenced delivery.
- Unreliable-sequenced delivery with final canonical convergence.
- Synthetic full change.
- Synthetic field mask.

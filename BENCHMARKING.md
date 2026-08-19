# Benchmarking SimNetLab

SimNetLab provides two headless Release builds for measuring snapshot replication without rendering
or Tracy instrumentation. They produce the same networking behavior as the corresponding headless
application configuration while keeping presentation work out of the measurement.

The public guide covers the output model and a basic matched run. The complete experiment design,
treatment matrix, repetition schedule, host setup, and statistical procedure belong with the
thesis and its capture archive.

## Headless builds

The `evidence` preset builds the ordinary Flecs simulation:

```sh
cmake --preset evidence
cmake --build --preset evidence
```

It produces:

```text
build/evidence/app/Server
build/evidence/app/Client
```

The `evidence-synthetic` preset enables the controlled synthetic snapshot workloads:

```sh
cmake --preset evidence-synthetic
cmake --build --preset evidence-synthetic
```

It produces the corresponding executables under `build/evidence-synthetic/app/`.

Both presets use a Release build with rendering, Tracy, and tests disabled. The ordinary preset
compiles synthetic workload support out. The synthetic preset compiles it in for the four shared
synthetic profiles under `config/`.

## Semantic CSV measurements

Semantic runs enable application CSV measurements. Server and Client must use the same shared
configuration and run ID.

Server replication CSV v4 records one row for each replication attempt. This includes successful
submissions, cadence skips, and terminal failures. The filename begins with
`server_replication_v4_`.

Client replication CSV v4 records one row for each Snapshot-lane application packet received by
the Client. Multi-packet updates therefore produce several Client rows. Per-packet decompression
values describe the current packet, while whole-update decompression appears on the row that
completes the packet group. The filename begins with `client_replication_v4_`.

One complete Server and Client run is one experimental sample. Tick, update, and packet rows are
measurements within that run. They are not independent repetitions.

The primary submitted-byte value is `transport_accepted_bytes` from successful Server rows. It
counts application payload bytes accepted by the transport API. It does not measure UDP, IP,
Ethernet, or other physical wire overhead. `transport_accepted_packet_count` similarly counts
accepted application packet submissions rather than physical datagrams.

Run-level analysis should aggregate the required fields over a declared measurement window. Ratios
should be calculated from aggregate numerators and denominators rather than by averaging row-level
ratios.

## Whole-process measurements

Whole-process `perf stat` measurements use separate Server and Client executions from semantic CSV
runs. Disable CSV output and logging for those executions so process counters do not include the
semantic instrumentation path.

Do not combine application elapsed-stage values with whole-process counters as if they came from
the same process execution. Application fields ending in `_elapsed_ns` are elapsed wall-time stage
measurements. They are not process CPU time.

## Matched ordinary run

Build the ordinary headless executables first:

```sh
cmake --preset evidence
cmake --build --preset evidence
```

Then start Server and Client in separate terminals with the same shared profile and run ID.

Server:

```sh
build/evidence/app/Server \
  --config config/server_default.json \
  --shared-config config/shared_demo_network.json \
  --run-id public-example \
  --max-ticks 600
```

Client:

```sh
build/evidence/app/Client \
  --config config/client_default.json \
  --shared-config config/shared_demo_network.json \
  --run-id public-example \
  --max-ticks 600
```

The tick limit above only keeps the example bounded. It is not an experiment prescription.

The default role configurations enable CSV output under `logs/`. Use resolved configuration files
with identical measurement settings when comparing treatments.

## Recording results

Archive enough information to reproduce and interpret every run:

- Exact shared, Server, and Client configurations
- Complete Server and Client commands
- Git revision and dirty state
- Configure and build presets
- Compiler and operating-system information
- Relevant host and network conditions
- Raw Server and Client output files
- Raw whole-process measurement output when collected

Rendering and Tracy are excluded from the headless evidence presets. Visual builds are useful for
demonstration and diagnosis, but their results should not be mixed with headless measurements.

The complete final methodology, including treatment construction, run ordering, validity rules,
and statistical analysis, should be distributed with the thesis and the private capture archive.

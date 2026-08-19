# SimNetLab

SimNetLab is a C++23 bachelor research project developed over approximately 14 weeks.

It investigates how snapshot replication techniques affect bandwidth, packet submissions,
representation quality, recovery behavior, and processing cost in a server-authoritative ECS
simulation.

## Techniques

- Area-of-interest selection with radius and field-of-view modes
- Every-tick and reduced snapshot cadence
- Distance-band level of detail
- Position quantization
- Octahedral heading encoding
- Bit-packed records
- Whole-record and field-mask Delta encoding
- Reliable and unreliable sequenced delivery
- No compression, whole-update Zstd, and per-packet Zstd
- Bounded application packetization
- Controlled synthetic Delta workloads

The [pipeline](src/pipeline/README.md) describes selection, representation, Delta, AOI, LOD, and
encoding. See [compression](src/compression/README.md), [packetization](src/packetization/README.md),
[transport](src/transport/README.md), and [synthetic workloads](src/synthetic/README.md) for details.

## Measurements

Server replication CSV v4 records one row per replication attempt. Client replication CSV v4 records
one row per received Snapshot-lane application packet. Analysis combines those rows into one result
per complete Server and Client run.

The headless builds exclude rendering and Tracy. Whole-process `perf stat` measurements use separate
executions from semantic CSV measurements. See [BENCHMARKING.md](BENCHMARKING.md) for details.

## Requirements

- Linux
- CMake and Ninja
- Git
- A C++23 compiler with named-module support
- Dependencies supplied through the vcpkg submodule
- Raylib only for optional rendering

GNU C++ 16.2.1 was used for final headless validation. The optional renderer was not part of that
measurement environment.

## Setup

Clone with submodules and run the bootstrap script:

```sh
git clone --recurse-submodules https://github.com/V4LKdev/SimNetLab.git
cd SimNetLab
./bootstrap.sh
```

The script initializes the vcpkg submodule, bootstraps vcpkg when needed, configures the Debug
preset, and builds it. The following CMake commands are the equivalent manual commands. Run the
test preset afterward:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Headless builds

```sh
cmake --preset evidence
cmake --build --preset evidence

cmake --preset evidence-synthetic
cmake --build --preset evidence-synthetic
```

Both presets produce headless Release Server and Client executables. Only the synthetic build
includes synthetic workload support.

## Headless run

Start Server and Client in separate terminals with the same shared profile and run ID.

```sh
build/evidence/app/Server \
  --config config/server_default.json \
  --shared-config config/shared_demo_network.json \
  --run-id example-headless \
  --max-ticks 600
```

```sh
build/evidence/app/Client \
  --config config/client_default.json \
  --shared-config config/shared_demo_network.json \
  --run-id example-headless \
  --max-ticks 600
```

The tick limit only keeps the example bounded. It is not an experiment prescription.

## Visual demo

With `relWithDebInfo` built, start the visual Server and Client in separate terminals:

```sh
build/relWithDebInfo/app/Server \
  --config config/server_visual.json \
  --shared-config config/shared_demo_visual.json
```

```sh
build/relWithDebInfo/app/Client \
  --config config/client_visual.json \
  --shared-config config/shared_demo_visual.json
```

Visual state is for demonstration and diagnosis. See [render](src/render/README.md) and
[configuration](config/README.md) for details.

## Structure

- `app/server` and `app/client` compose the executable runtimes.
- `simnet_core` defines bytes, identifiers, math, and fixed-step types.
- `simnet_runtime` plans fixed-step frames and evaluates runtime limits.
- `simnet_config` loads strict JSON configuration and computes fingerprints.
- `simnet_snapshot` defines complete snapshots, patches, validation, and reconstruction.
- `simnet_pipeline` selects, transforms, encodes, and decodes snapshot updates.
- `simnet_compression` provides bounded Raw and ordinary Zstd envelopes.
- `simnet_packetization` splits and reassembles bounded application packet groups.
- `simnet_transport` provides ENet sessions and opaque byte delivery.
- `simnet_game_server` runs the authoritative Flecs simulation.
- `simnet_game_client` applies reconstructed state to the Client Flecs sink.
- `simnet_spatial` provides deterministic sparse-grid queries.
- `simnet_synthetic` creates controlled deterministic snapshot workloads.
- `simnet_telemetry` provides logging and versioned CSV persistence.
- `simnet_render` provides the optional Raylib demonstration.

## Limitations

- SimNetLab is Linux-focused.
- ENet is the only transport.
- Application payload bytes are not physical wire bytes.
- The Client has no prediction.
- Rendering is demonstrative and excluded from measurements.
- Packet-loss recovery requires controlled and documented network impairment.
- SimNetLab is a thesis prototype rather than release software.

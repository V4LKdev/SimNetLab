# SimNetLab

SimNetLab is a C++23 bachelor research project for evaluating snapshot replication techniques in a server-authoritative ECS simulation.

The current foundation separates core vocabulary, fixed-step runtime planning, configuration, snapshots, pipeline encoding, ENet transport, telemetry, and Flecs game contracts. Server and Client are the composition boundary.

## Current capability

- ENet-only transport with session handshake, bounded payload policy, SnapshotAck messages, and reliable opaque application control
- Fixed-step Server and Client runtime loops with bounded frame, tick, and duration limits
- One authoritative server peer and one client peer
- Pipeline-library support for full replacement, incremental selection, quantization, octahedral heading encoding, delta snapshots, and bit-packed records
- Application configuration for full replacement, incremental selection, quantization, and exact-baseline delta reconstruction
- Catch2 coverage for runtime timing, pipeline behavior, transport session behavior, and replication contracts
- 1,000-entity Server to Client replication in the bounded runtime path
- Optional Server and Client visualization with instanced directional entities, stable entity navigation, paged panels, local debug observer views, and authoritative remote pause

The Server currently advances the authoritative boid state but does not yet implement the intended boid behavior model.

## Planned work

Area of interest, LOD, compression, benchmarking, and metrics export remain planned. Tracy instrumentation is available through the `SIMNET_ENABLE_TRACY` CMake option. Server and Client viewers are available when local visualization is enabled. The Server can display its current occupied spatial cells for debugging only.

`Aoi`, `Lod`, and `Compression` remain declared pipeline vocabulary. They are not implemented and a selected unsupported pipeline option is rejected during app startup.

BitPacking is retained as an evaluated technique. The current quantized octahedral record is already 120 bits, so bit packing produces the same 15-byte record size while adding packing work.

## Requirements

- Linux
- CMake 4.3 or newer
- Ninja
- Git
- A C++23 compiler with C++ module support
- Raylib development package when render support is enabled

Other dependencies are managed through the vcpkg submodule.

## Setup

```sh
git clone --recurse-submodules https://github.com/V4LKdev/SimNetLab.git
cd SimNetLab
./bootstrap.sh
```

The bootstrap script initializes vcpkg, configures the Debug preset, and builds the applications, libraries, and tests.

To select another preset:

```sh
./bootstrap.sh relWithDebInfo
./bootstrap.sh release
```

## Build and test

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Both applications accept `--config PATH`, `--shared-config PATH`, `--max-ticks`, `--max-frames`, and `--max-runtime-ms`. Server additionally accepts `--max-frame-delta-ms` and `--max-steps-per-frame`. A zero limit is disabled.

The default profiles are headless. To start the visual development profiles from the repository root:

```sh
build/debug/app/Server --config config/server_visual.json
build/debug/app/Client --config config/client_visual.json
```

The Server visual profile uses the tracked `assets/render/boid.obj` mesh. Set the local `visualization.entity_mesh_path` to another OBJ file to replace it. An empty or unavailable path keeps the instanced wedge fallback.

## Project structure

- `simnet_core`: dependency-free math, time, bytes, and identifiers
- `simnet_runtime`: frame planning, run limits, counters, and stop state
- `simnet_config`: JSON configuration and compatibility fingerprints
- `simnet_snapshot`: replicated world snapshots and client patches
- `simnet_synthetic`: deterministic snapshot generation
- `simnet_telemetry`: logging, metrics storage, and profiling hooks
- `simnet_spatial`: sparse uniform-grid queries
- `simnet_game_shared`: shared Flecs contracts
- `simnet_game_server`: authoritative extraction
- `simnet_game_client`: client patch application and replicated-world extraction
- `simnet_pipeline`: snapshot selection, transformation, encoding, and decoding
- `simnet_transport`: ENet transport and session protocol
- `simnet_render`: generic core-only Raylib viewer
- `simnet_benchmarking`: benchmarking placeholder

The placeholder benchmarking target is disabled by default. Enable
`SIMNET_ENABLE_BENCHMARKING` only when working on the future benchmark harness.
The current JSON metrics-export and benchmark settings are parsed configuration
vocabulary; the applications do not yet export metrics or execute benchmark
scenarios.

Default configuration is in `config/shared_default.json`, `config/server_default.json`, and `config/client_default.json`. `config/server_visual.json` and `config/client_visual.json` enable the same local visualization settings without changing simulation, pipeline, or transport configuration.

Server-local `flecs.thread_count` defaults to one. Values above one enable Flecs worker scheduling for systems explicitly marked as multithreaded; they do not parallelize queries or application code automatically.

For renderer stress testing, use the 100,000-entity shared profile with the visual Server profile:

```sh
build/relWithDebInfo/app/Server --config config/server_visual.json --shared-config config/shared_stress_100k.json
```

Use the same `--shared-config` value for Client when connecting it to a non-default Server.

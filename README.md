# SimNetLab

SimNetLab is a C++23 bachelor research project for evaluating snapshot replication techniques in a server-authoritative ECS simulation.

The finished foundation separates core vocabulary, fixed-step runtime planning, configuration, snapshots, pipeline encoding, ENet transport, telemetry, and Flecs game contracts. Server and Client are the composition boundary.

## Current capability

- ENet-only transport with session handshake, bounded payload policy, SnapshotAck messages, and reliable opaque application control
- Fixed-step Server and Client runtime loops with bounded frame, tick, and duration limits
- One authoritative server peer and one client peer
- Full replacement, incremental selection, quantization, octahedral heading encoding, delta snapshots, and bit-packed records
- Catch2 coverage for runtime timing, pipeline behavior, transport session behavior, and replication contracts
- 1,000-entity Server to Client replication in the bounded runtime path
- Optional Server and Client visualization with instanced directional entities, stable entity navigation, paged panels, local debug observer views, and authoritative remote pause

The Server currently advances the authoritative boid state but does not yet implement the intended boid behavior model.

## Planned work

Area of interest, LOD, compression, benchmarking, metrics export, and Tracy instrumentation remain planned. Server and Client viewers are available when local visualization is enabled. The Server can display its current occupied spatial cells for debugging only.

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

The bootstrap script initializes vcpkg, configures the Debug preset, and builds Server and Client.

To select another preset:

```sh
./bootstrap.sh relWithDebInfo
./bootstrap.sh release
```

## Build and test

```sh
cmake --preset debug
cmake --build --preset debug --target Server Client
ctest --test-dir build/debug --output-on-failure
```

Server accepts `--config PATH`, `--max-ticks`, `--max-frames`, `--max-runtime-ms`, `--max-frame-delta-ms`, and `--max-steps-per-frame`. Client accepts `--config PATH`, `--max-ticks`, `--max-frames`, and `--max-runtime-ms`. A zero limit is disabled.

The default profiles are headless. To start the visual development profiles from the repository root:

```sh
build/debug/app/Server --config config/server_visual.json
build/debug/app/Client --config config/client_visual.json
```

Set the local `visualization.entity_mesh_path` to an OBJ file to replace the procedural wedge. An empty or unavailable path keeps the instanced wedge fallback.

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

Default configuration is in `config/shared_default.json`, `config/server_default.json`, and `config/client_default.json`. `config/server_visual.json` and `config/client_visual.json` enable the same local visualization settings without changing simulation, pipeline, or transport configuration.

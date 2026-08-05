# SimNetLab

SimNetLab is a C++23 bachelor research project for evaluating snapshot replication techniques in a server-authoritative ECS simulation.

The current foundation separates core vocabulary, fixed-step runtime planning, configuration, snapshots, pipeline encoding, ENet transport, telemetry, and Flecs game contracts. Server and Client are the composition boundary.

## Current capability

- ENet-only transport with session handshake, bounded payload policy, SnapshotAck messages, reliable opaque application control, and unreliable-sequenced opaque player input
- Fixed-step Server and Client runtime loops with bounded frame, tick, and duration limits
- One authoritative server peer and one client peer
- Pipeline-library support for full replacement, incremental selection, quantization, octahedral heading encoding, delta snapshots, and bit-packed records
- Application configuration for full replacement, incremental selection, quantization, and exact-baseline delta reconstruction
- Catch2 coverage for runtime timing, pipeline behavior, transport session behavior, and replication contracts
- Deterministic Server-authoritative boids with switchable separation, alignment, cohesion, containment, wander, and circular hue behavior
- 1,000-entity Server to Client replication in the bounded runtime path
- Optional interpolated Server and Client visualization with instanced directional entities, stable entity navigation, paged panels, local stationary observer views, and authoritative remote pause
- Stationary observer and authoritative player join roles with one Server-owned player fish and a locked third-person Client chase camera

## Planned work

Area of interest, LOD, compression, the repeatable benchmark runner, and external experiment
orchestration remain planned. Tracy instrumentation is available through the
`SIMNET_ENABLE_TRACY` CMake option. Server and Client viewers are available when local
visualization is enabled. The Server can display occupied spatial cells and selected-boid
simulation diagnostics.

BitPacking is retained as an evaluated technique. The current quantized octahedral record is already 128 bits, so bit packing produces the same 16-byte record size while adding packing work.

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

Both applications accept `--config PATH`, `--shared-config PATH`, `--run-id TEXT`, `--max-ticks`,
`--max-frames`, and `--max-runtime-ms`. Server additionally accepts `--max-frame-delta-ms` and
`--max-steps-per-frame`. A zero limit is disabled. A supplied run ID correlates Server and Client
CSV evidence. Without it, each process generates an independent process-local ID.

The default profiles are headless. To start the visual development profiles from the repository root:

```sh
build/debug/app/Server --config config/server_visual.json
build/debug/app/Client --config config/client_visual.json
```

To join as the one supported player instead of a stationary observer:

```sh
build/debug/app/Client --config config/client_player_visual.json
```

The player camera is locked behind and above the replicated fish. `W`/`S` apply
pitch steering, `A`/`D` apply yaw steering, Shift accelerates, and Ctrl slows.
Angular velocity, damping, and rate limits give steering inertia, while
Shift+Ctrl selects cruise speed.
Input is latest-state unreliable-sequenced data, while join and pause remain
reliable controls. `C` opens the cameras available to the current role and
selection. `F4` opens the read-only Setup inspector. Leaving Game sends one
neutral input state. There is no
client prediction yet.

The Server visual profile uses the tracked `assets/render/boid.obj` mesh. Set the local `visualization.entity_mesh_path` to another OBJ file to replace it. An empty or unavailable path keeps the instanced wedge fallback.

## Project structure

- `simnet_core`: dependency-free math, time, bytes, and identifiers
- `simnet_runtime`: frame planning, run limits, counters, and stop state
- `simnet_config`: JSON configuration and compatibility fingerprints
- `simnet_snapshot`: replicated world snapshots and client patches
- `simnet_synthetic`: deterministic snapshot generation
- `simnet_telemetry`: logging, typed measurement contracts, and profiling hooks
- `simnet_spatial`: sparse uniform-grid queries
- `simnet_game_shared`: shared Flecs contracts
- `simnet_game_server`: authoritative lifecycle, boid simulation, and snapshot extraction
- `simnet_game_client`: client patch application and replicated-world extraction
- `simnet_pipeline`: snapshot selection, transformation, encoding, and decoding
- `simnet_transport`: ENet transport and session protocol
- `simnet_render`: generic core-only Raylib viewer
- `simnet_benchmarking`: benchmarking placeholder

The placeholder benchmarking target is disabled by default. Enable
`SIMNET_ENABLE_BENCHMARKING` only when working on the future benchmark harness.
Server and Client write separate v1 replication CSVs under `telemetry.log_directory` when
`metrics_csv_enabled` is true. The Server also writes a small boid-tuning CSV containing one
aggregate row per simulated second. These files are raw application evidence, not the future
benchmark runner. Reserved benchmark settings are not part of the runtime measurement contract.

Default configuration is in `config/shared_default.json`, `config/server_default.json`, and `config/client_default.json`. `config/server_visual.json` and `config/client_visual.json` enable the same local visualization settings without changing simulation, pipeline, or transport configuration. `config/client_player_visual.json` requests the player role. Stationary observer remains the default.

Server-local `flecs.thread_count` defaults to one. Values above one enable Flecs worker scheduling for systems explicitly marked as multithreaded. They do not parallelize queries or application code automatically.

For the conservative 1,000-boid visual demonstration:

```sh
build/relWithDebInfo/app/Server --config config/server_visual.json --shared-config config/shared_boids_demo.json
build/relWithDebInfo/app/Client --config config/client_visual.json --shared-config config/shared_boids_demo.json
```

The Server keeps velocity, precise hue phase, and neighbor-computation state private. Replication remains the stable ID, position, normalized heading, and one-byte hue snapshot contract. The Server selected-entity panel remains available while paused and shows velocity, acceleration, query and neighbor counts, rule settings, flags, hue decisions, and steering contributions. Its optional generic gizmos show the queried cells, rule radii, FOV, and steering vectors. The Client does not receive those private facts.

Visual interpolation is local and presentation-only. The Server blends adjacent authoritative ticks with the fixed-step alpha. The Client renders directly from retained reconstructed snapshots and derives its interval from snapshot ticks. It does not query the replicated Flecs world every frame. Pause snaps both viewers to their latest exact state. Set `visualization.interpolation_enabled` to `false` for exact snapshot debugging.

For local high-count stress and visual inspection, use the 50,000-entity shared profile with the visual Server profile. This is not a realistic network-replication workload:

```sh
build/relWithDebInfo/app/Server --config config/server_visual.json --shared-config config/shared_stress_50k.json
```

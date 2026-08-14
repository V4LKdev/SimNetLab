# SimNetLab

SimNetLab is a C++23 bachelor research project for evaluating snapshot replication techniques in a server-authoritative ECS simulation.

The current foundation separates core vocabulary, fixed-step runtime planning, configuration, snapshots, pipeline encoding, ENet transport, telemetry, and Flecs game contracts. Server and Client are the composition boundary.

## Current capability

- ENet transport with a session handshake and generic bounded post-session byte lanes
- Versioned application messages for acknowledgments, control, player input, and stationary observer interest
- Configurable hard application packetization with bounded complete-group Client reassembly
- Toggleable bounded Zstd compression for complete updates or individual application packets, with
  an optional maintained `pipeline_v1` whole-update dictionary
- Fixed-step Server and Client runtime loops with bounded frame, tick, and duration limits
- Bounded multi-client Server coordination with isolated Player and stationary observer sessions
- Pipeline-library support for full replacement, incremental selection, quantization, octahedral heading encoding, delta snapshots, and bit-packed records
- Application configuration for cadence, full replacement, incremental selection, quantization, octahedral headings, bit packing, exact-baseline delta reconstruction, radius or conical-FOV AOI, and packetization
- Catch2 coverage for runtime timing, pipeline behavior, transport session behavior, and replication contracts
- Deterministic Server-authoritative boids with switchable separation, alignment, cohesion, containment, wander, and circular hue behavior
- 1,000-entity Server to Client replication in the bounded runtime path
- Optional interpolated Server and Client visualization with instanced directional entities, stable entity navigation, paged panels, local stationary observer views, and authoritative remote pause
- Stationary observer and authoritative Player join roles with per-session Player ownership and a locked third-person Client chase camera

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

The developer-only Server option `--compression-corpus-dir PATH` captures whole-update compression
inputs for offline dictionary training.

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
neutral input state immediately. The Client also repeats the latest active or
neutral state every 100 ms, including when rendering is disabled. There is no
client prediction yet.

The Server visual profile uses the tracked `assets/render/boid.obj` mesh. Set the local `visualization.entity_mesh_path` to another OBJ file to replace it. An empty or unavailable path keeps the instanced wedge fallback.

For the matched radius-AOI representation control, start both processes with the same shared
profile:

```sh
build/debug/app/Server --config config/server_visual.json --shared-config config/shared_representation_raw_aoi_radius_visual.json
build/debug/app/Client --config config/client_visual.json --shared-config config/shared_representation_raw_aoi_radius_visual.json
```

Replace the shared profile with `shared_representation_quantized_aoi_radius_visual.json`,
`shared_representation_oct_heading_aoi_radius_visual.json`,
`shared_representation_bit_packed_aoi_radius_visual.json`, or
`shared_cadence_reduced_aoi_radius_visual.json` for each matched treatment. The bit-packed and
byte-aligned octahedral records are both 16 bytes. The reduced-cadence treatment emits every four
authoritative ticks.

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
- `simnet_compression`: bounded Raw, ordinary Zstd, and dictionary Zstd byte envelopes
- `simnet_packetization`: bounded opaque byte groups and Client reassembly
- `simnet_transport`: ENet transport and session protocol
- `simnet_render`: generic core-only Raylib viewer

Benchmark orchestration is outside the production library graph. Server and Client expose bounded
runtime controls and versioned CSV evidence for external automation and collectors. The Server
also writes a small boid-tuning CSV containing one aggregate row per simulated second.

Default configuration is in `config/shared_default.json`, `config/server_default.json`, and `config/client_default.json`. `config/server_visual.json` and `config/client_visual.json` enable the same local visualization settings without changing simulation, pipeline, or transport configuration. `config/client_player_visual.json` requests the player role. Stationary observer remains the default.

Server-local `flecs.thread_count` defaults to one. Values above one enable Flecs worker scheduling for systems explicitly marked as multithreaded. They do not parallelize queries or application code automatically.

For a visual demonstration profile:

```sh
build/relWithDebInfo/app/Server --config config/server_visual.json --shared-config config/shared_demo_visual.json
build/relWithDebInfo/app/Client --config config/client_visual.json --shared-config config/shared_demo_visual.json
```

The Server keeps velocity, precise hue phase, and neighbor-computation state private. Replication remains the stable ID, position, normalized heading, and one-byte hue snapshot contract. The Server selected-entity panel remains available while paused and shows velocity, acceleration, query and neighbor counts, rule settings, flags, hue decisions, and steering contributions. Its optional generic gizmos show the queried cells, rule radii, FOV, and steering vectors. The Client does not receive those private facts.

Visual interpolation is local and presentation-only. The Server blends adjacent authoritative ticks with the fixed-step alpha. The Client renders directly from retained reconstructed snapshots and derives its interval from snapshot ticks. It does not query the replicated Flecs world every frame. Pause snaps both viewers to their latest exact state. Set `visualization.interpolation_enabled` to `false` for exact snapshot debugging.

For local high-count stress and visual inspection, use the 50,000-entity shared profile with the visual Server profile. This is not a realistic network-replication workload:

```sh
build/relWithDebInfo/app/Server --config config/server_visual.json --shared-config config/shared_stress_50k.json
```

# SimNetLab

SimNetLab is a work-in-progress C++23 research project for evaluating network replication techniques in a deterministic,
server-authoritative ECS simulation.

The project uses a Flecs-based boid simulation as its test environment and separates simulation, snapshot generation,
replication pipelines, transport, telemetry, and rendering into independent modules.

## Current Features

- Server-authoritative Flecs simulation
- Server and client applications
- ENet transport backend
- Linux local IPC transport backend
- Connection handshake and snapshot acknowledgements
- Full and partial snapshot replication
- Send-interval control
- Incremental entity selection
- Position quantization
- Octahedral heading encoding
- Bit-packed record encoding
- Acknowledged-snapshot delta baselines
- Pipeline validation and wire-format signatures
- Synthetic snapshot generation
- Logging, metrics, and optional Tracy profiling
- Optional Raylib visualization

Area-of-interest filtering, LOD, compression, and automated benchmarking are planned or currently under development.

## Tested Requirements

- Linux
- CMake 4.3 or newer
- Ninja
- Git
- A C++23 compiler with C++ module support
- Raylib development package

Other dependencies are managed through the vcpkg submodule.

## Setup

```sh
git clone --recurse-submodules https://github.com/V4LKdev/SimNetLab.git
cd SimNetLab
./bootstrap.sh
```

The bootstrap script initializes vcpkg, configures the debug preset, and builds the Server and Client targets.

Another preset can be selected explicitly:

```sh
./bootstrap.sh relWithDebInfo
./bootstrap.sh release
Manual Build
```

Available configure and build presets:

```sh
cmake --list-presets
```

Configure and build:

```sh
cmake --preset debug
cmake --build --preset debug --target Server Client
```

Additional validation executables can be built with:

```sh
cmake --build --preset debug --target PipelineLab TransportSmoke
```

Build directories:

build/debug build/relWithDebInfo build/release Build Options

Important CMake options include:

SIMNET_WARNINGS_AS_ERRORS SIMNET_ENABLE_ASAN SIMNET_ENABLE_UBSAN SIMNET_ENABLE_TRACY SIMNET_ENABLE_RENDER
SIMNET_ENABLE_BENCHMARKING SIMNET_ENABLE_ENET SIMNET_ENABLE_LOCAL_IPC

Example:

```sh
cmake --preset debug \
-DSIMNET_ENABLE_TRACY=ON \
-DSIMNET_WARNINGS_AS_ERRORS=ON
```

Project Structure

<b> simnet_core </b> - fundamental math, time, byte, and identifier types <br/>
<b> simnet_config </b> - typed runtime configuration and compatibility fingerprints <br/>
<b> simnet_snapshot </b> - canonical world snapshots and client snapshot patches <br/>
<b> simnet_synthetic </b> - deterministic synthetic snapshot generation <br/>
<b> simnet_telemetry </b> - logging, metrics, and profiling hooks <br/>
<b> simnet_spatial </b> - spatial query structures <br/>
<b> simnet_game_shared </b> - shared Flecs components and contracts <br/>
<b> simnet_game_server </b> - authoritative simulation <br/>
<b> simnet_game_client </b> - replicated client world <br/>
<b> simnet_pipeline </b> - snapshot selection, transformation, encoding, and decoding <br/>
<b> simnet_transport </b> - transport abstraction, ENet, and local IPC <br/>
<b> simnet_render </b> - optional Raylib visualization <br/>
<b> simnet_benchmarking </b> - benchmarking infrastructure under development <br/>

Default runtime configuration is stored in:

```
config/shared_default.json 
config/server_default.json 
config/client_default.json Status
```

SimNetLab is under active development as part of a bachelor research project. 
Interfaces, techniques, configuration, and
build requirements may change as the implementation and evaluation progress.
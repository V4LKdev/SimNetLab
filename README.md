# SimNetLab

C++23 networked simulation project.

## Requirements

- CMake 4.3 or newer
- A C++23 compiler with C++ module support
- Ninja
- Git
- vcpkg, provided as the repo-local `vcpkg/` submodule

On Linux, `raylib` is expected from the system because the vcpkg version is not working well under wayland.

## First Setup

Clone the repo:

```sh
git clone <repo-url>
cd Thesis
```

Run the setup script:

```sh
./bootstrap.sh
```

On Windows PowerShell:

```powershell
.\bootstrap.ps1
```

This initializes submodules, bootstraps vcpkg, configures `debug`, and builds `Server` and `Client`.

Pass another preset name to set up a different profile, for example `./bootstrap.sh release`.

## Configure

Available presets:

```sh
cmake --list-presets
```

Configure one profile:

```sh
cmake --preset debug
cmake --preset relWithDebInfo
cmake --preset release
```

Build directories are:

- `build/debug`
- `build/relWithDebInfo`
- `build/release`

## Build

Build the current apps:

```sh
cmake --build --preset debug --target Server Client
```

Current app targets:

- `Server`
- `Client`

## CMake Options

```sh
cmake --preset debug -DSIMNET_WARNINGS_AS_ERRORS=ON
cmake --preset debug -DSIMNET_ENABLE_ASAN=ON
cmake --preset debug -DSIMNET_ENABLE_UBSAN=ON
cmake --preset debug -DSIMNET_ENABLE_TRACY=ON
cmake --preset debug -DSIMNET_ENABLE_RENDER=OFF
cmake --preset debug -DSIMNET_ENABLE_BENCHMARKING=OFF
cmake --preset debug -DSIMNET_ENABLE_ENET=OFF
```

Default runtime configs live in `config/shared_default.json`, `config/server_default.json`, and `config/client_default.json`.

## Modules

- `simnet_core`: basic types and utilities
- `simnet_config`: typed config and JSON
- `simnet_telemetry`: logging, profiling hooks, metrics
- `simnet_snapshot`: snapshot and packet data
- `simnet_spatial`: spatial query structures
- `simnet_game_shared`: shared ECS contracts
- `simnet_game_server`: authoritative simulation
- `simnet_game_client`: client-side world
- `simnet_pipeline`: AoI, delta, serialization, compression pipeline
- `simnet_transport`: transport and ENet backend
- `simnet_render`: raylib visualization
- `simnet_benchmarking`: benchmark helpers

Dependency ownership:

- JSON is owned by `simnet_config`
- spdlog and optional Tracy are owned by `simnet_telemetry`
- Flecs is owned by `simnet_game_shared`
- ENet is owned by `simnet_transport`
- raylib is owned by `simnet_render`
- Google Benchmark is owned by `simnet_benchmarking`

# SimNetLab

SimNetLab is a C++23 bachelor research project developed over approximately 12 weeks by one
student. It investigates how snapshot replication techniques affect application bandwidth,
application packet submissions, representation quality, recovery behavior, and coarse processing
cost in a Server-authoritative ECS simulation.

## Technique dimensions

The retained comparison dimensions are:

- Area of interest using radius or conical field of view selection
- Snapshot emission cadence
- Temporal distance-band level of detail
- Raw, quantized, octahedral, and bit-packed representation
- Whole-record and field-mask Delta encoding
- Reliable-sequenced and unreliable-sequenced delivery
- No compression, whole-update ordinary Zstd, and per-packet ordinary Zstd
- Bounded application packetization
- Four controlled synthetic Delta workloads

Detailed contracts live with the [pipeline](src/pipeline/README.md),
[compression](src/compression/README.md), [packetization](src/packetization/README.md),
[transport](src/transport/README.md), and [synthetic workload](src/synthetic/README.md) modules.

## Evidence model

Server replication CSV v4 records one row per replication attempt. Client replication CSV v4
records one row per received Snapshot-lane application packet. Final statistical analysis uses
run-level aggregates rather than treating individual tick or packet rows as independent subjects.

Evidence builds exclude rendering. Direct whole-process perf measurements use separate process
executions from semantic CSV runs. The authoritative collection procedure, fixed tick window,
validity rules, commands, and archive convention are in
[EVIDENCE_PROTOCOL.md](EVIDENCE_PROTOCOL.md).

## Supported environment

Code-freeze support is limited to:

- Linux
- CMake and Ninja
- Git
- A C++23 compiler with named-module support
- GNU C++ 16.2.1 as the code-freeze validation compiler
- Raylib only when optional rendering is enabled
- Project dependencies supplied through the vcpkg submodule

Headless Debug, ordinary evidence, and synthetic evidence builds were validated at code freeze.
The optional renderer was not part of the final headless evidence validation environment.

## Setup

Clone with submodules and build the default Debug preset:

```sh
git clone --recurse-submodules https://github.com/V4LKdev/SimNetLab.git
cd SimNetLab
./bootstrap.sh
```

The equivalent direct commands are:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Dependencies are resolved through `vcpkg/`.

## Evidence builds

Build the ordinary headless Release binaries with:

```sh
cmake --preset evidence
cmake --build --preset evidence
```

Build the controlled synthetic workloads with:

```sh
cmake --preset evidence-synthetic
cmake --build --preset evidence-synthetic
```

Both presets disable rendering, Tracy, tests, and sanitizers. The ordinary preset compiles
synthetic support out. The synthetic preset enables it for the four tracked synthetic profiles.

## One matched headless run

Run Server and Client in separate terminals with the same shared profile and run ID:

```sh
build/evidence/app/Server \
  --config config/server_default.json \
  --shared-config config/shared_compression_none_aoi_radius_visual.json \
  --run-id example-no-compression \
  --max-ticks 2460
```

```sh
build/evidence/app/Client \
  --config config/client_default.json \
  --shared-config config/shared_compression_none_aoi_radius_visual.json \
  --run-id example-no-compression \
  --max-ticks 2460
```

Use the archived logging configuration, 30 repetitions, treatment ordering, and aggregation rules
from the [final evidence protocol](EVIDENCE_PROTOCOL.md) for thesis data collection.

## Optional visual demonstration

After building the `relWithDebInfo` preset with rendering enabled, run:

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

`config/client_player_visual.json` requests the optional Player role. Visual state is local
presentation only and is excluded from final evidence builds. See the
[render](src/render/README.md) and [configuration](config/README.md) references.

## Project structure

- `simnet_core` provides bytes, identifiers, math, and fixed-step vocabulary.
- `simnet_runtime` provides frame planning, limits, counters, and stop state.
- `simnet_config` loads strict JSON configuration and computes fingerprints.
- `simnet_snapshot` owns complete snapshots, patches, validation, and reconstruction.
- `simnet_pipeline` selects, transforms, encodes, and decodes snapshot updates.
- `simnet_compression` provides bounded Raw and ordinary Zstd envelopes.
- `simnet_packetization` provides bounded application packet groups and reassembly.
- `simnet_transport` provides ENet sessions and opaque byte delivery.
- `simnet_game_server` owns authoritative Flecs simulation and snapshot extraction.
- `simnet_game_client` applies reconstructed state to the nonauthoritative Flecs sink.
- `simnet_spatial` provides deterministic sparse-grid queries.
- `simnet_synthetic` provides controlled deterministic snapshot workloads.
- `simnet_telemetry` owns logging and v4 CSV persistence contracts.
- `simnet_render` is the optional Raylib demonstrator.
- `app/server` and `app/client` compose the executable runtimes.

## Limitations

- The project is Linux-focused.
- ENet is the only retained transport.
- Transport-accepted application bytes are not physical network wire bytes.
- The Client has no prediction.
- Rendering is demonstrative and excluded from evidence.
- Unreliable recovery efficacy requires actual packet impairment with the command archived.
- SimNetLab is a thesis prototype rather than a release product.

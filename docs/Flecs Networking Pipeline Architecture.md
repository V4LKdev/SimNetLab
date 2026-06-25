## Overview

The server ECS pipeline is extended to feed a modular, per‑peer networking chain _without_ redundant data copies.  
After the simulation tick, a single SoA buffer (`NetworkSnapshot`) is built from final boid components.  
Per‑peer visibility indices are computed (optionally via the same spatial grid used for neighbour search).  
Each peer then runs a config‑driven, stateless pipeline of filters → serialiser → post‑encoder → ENet send.

## Tick Phases (server_world.cpp)

```
Preload
├─ BoidPopulationManager
└─ IncrementSimTick

SimPrepare
├─ ClearAccumulate
├─ AlignHeading
└─ BuildNeighborCache          ← O(n²) brute‑force → spatial grid

SimCompute
├─ Alignment   (reads NeighborCache)
├─ Cohesion    (reads NeighborCache)
└─ Separation  (reads NeighborCache)

SimApply
├─ ApplySteering
└─ IntegratePosition            ← final positions for this tick

SimPost
└─ (telemetry, optional)

NetSend                         ← depends_on SimPost
├─ NetPrepareSnapshot           ← NEW (replaces old BuildGlobalSnapshot)
├─ NetComputePeerVisibility     ← NEW
└─ NetPipeline                  ← NEW (replaces old SendSnapshots)
```

## Singletons (ECS)

- `SimTick` – current tick counter
- `Config::SimConfig` – all run‑time options (including technique flags)
- `NeighborCache` – positions/headings/neighbour lists (built during `SimPrepare`)
- `NetworkSnapshot` – **new**: compact SoA buffer of network‑relevant fields, built once per tick
- `PeerStates` – **new**: map of per‑peer metadata (visibility indices, delta baselines, …)
- `MetricsCollector` – **new**: accumulates bandwidth/CPU stats for benchmarking

## Data Flow

```
[All boids after SimApply]
   │
   ▼
NetPrepareSnapshot  (once per tick, multithreaded ok)
   • Queries Position, Velocity, Heading, Hue, NetworkId from ECS tables
   • Copies and optionally pre‑quantises values into NetworkSnapshot (SoA)
   • Leaves previous GlobalSnapshot (if needed) for delta compression

         NetworkSnapshot (read‑only)
              │
   ┌──────────┴──────────┐
   │  Peer 1             │  Peer 2  …  Peer N
   ▼                     ▼
NetComputePeerVisibility (per peer, multithreaded via job system or manual dispatch)
   • If AoI enabled → queries spatial grid with peer viewport → list of entity indices
   • Else → full index list [0 .. N‑1]
   • Stores visible_indices in PeerState

   visible_indices
        │
        ▼
   [ Filter stage ]   ← IEntityFilter   (LOD, dirty‑flags, leader‑follower removal)
        │
        ▼
   [ Serializer ]     ← ISerializer     (delta compression, bit packing, incremental)
        │
        ▼
   [ Post‑encoder ]   ← IEncoder        (full‑snapshot compression, Zstd, Huffman)
        │
        ▼
   ENet send
```

- **Zero per‑peer copy** of entity data – all stages read directly from the global `NetworkSnapshot`.
- Filters and serialisers work only on index lists and per‑peer state.

## Pipeline Configuration

The existing `SimConfig` (sim_config.hpp) already contains boolean flags for every technique:

|Flag|Stage affected|
|---|---|
|`enable_aoi_filter`|NetComputePeerVisibility & filter chain|
|`enable_entity_lod`|Filter stage (tiered index lists)|
|`enable_delta_compression`|Serializer (uses per‑peer baseline in PeerState)|
|`enable_full_snapshot_compression`|Post‑encoder|
|`enable_float_quantization`|NetPrepareSnapshot (pre‑quantisation) or Serializer|
|`enable_octahedral_heading`|NetPrepareSnapshot or Serializer|
|`enable_incremental_snapshots`|Filter stage|
|`enable_leader_follower`|Filter stage (removes followers)|
|…|…|

A `PipelineFactory` reads the config and constructs the chain for each peer:

cpp

```
auto chain = PipelineFactory::create(cfg);
chain.add_default_stages();
if (cfg.enable_aoi_filter) chain.insert(0, std::make_unique<AoIFilter>());
if (cfg.enable_delta_compression) chain.set_serializer<DeltaSerializer>();
// ...
```

## Per‑Peer Parallelisation

- The per‑peer pipeline stages are **stateless** and share no mutable data.
- `NetPipeline` system itself can fan out peers to a thread pool (`std::async`, `BS::thread_pool`, or a custom task system).  
    Flecs’ per‑system multithreading cannot split a single system across peers, but in‑system parallelism is easy.
- Because `NetworkSnapshot` is read‑only and each `PeerState` is independent, no locks are needed.

## Future Spatial Grid Integration

Currently neighbour search uses brute‑force O(n²).  
Planned replacement with a spatial partition (grid, quadtree, …).

- The partition will be **built/updated once** (likely after `IntegratePosition` in `SimPost`).
- **Simulation**: neighbour queries run in `SimPrepare` of the _next_ tick (using data from previous tick’s final positions – typical for fixed‑step ECS).
- **Networking**: AoI visibility queries use the _same_ grid in `NetComputePeerVisibility` during the same tick.

Result: one data structure serves both simulation and networking, avoiding duplication.

## Metrics & Benchmarking

- Every pipeline stage logs execution time and produced byte sizes into `MetricsCollector` (singleton).
- The whole `NetSend` phase can be wrapped by Google Benchmark fixtures.
- Config variations (which techniques are on/off) are driven by the JSON config, enabling automated multi‑configuration testing as per the academic test plan (Phases 1‑4).

---

The network pipeline operates on **one immutable `NetworkSnapshot` per tick** and generates a per‑peer byte stream with zero copying of entity data.  
The architecture is layered: **filter → serialise → post‑encode**, all driven by config toggles and scheduled through Flecs systems.

### Global Data Structures

- **`NetworkSnapshot`** – SoA buffers (entity IDs, positions, headings, hues) populated **once per tick** by `NetPrepareSnapshot`. No velocities (not needed on clients). Inserted into history ring buffer.
- **`SnapshotHistory`** – fixed‑size (32‑tick) ring buffer of shared snapshots. Enables O(1) delta baseline lookup per peer, without per‑peer copies of baselines.
- **`PeerConnectionState`** – per‑peer struct containing `peer_id`, sequence counter, `last_acknowledged_tick` (key into history), viewport center, and `visible_indices`. Typed, not `std::any`. Mutated only on main thread; worker threads read it.

### Pipeline Interfaces

cpp

```
class IEntityFilter {
    virtual void filter(PipelineContext& ctx) = 0; // modifies ctx.active_indices
};
class ISerializer {
    virtual void serialize(const PipelineContext& ctx, NetBuffer& out) = 0;
};
class IEncoder {
    virtual void encode(const NetBuffer& in, NetBuffer& out) = 0;
};
```

- **PipelineContext** bundles peer state, current snapshot, optional baseline snapshot, and the mutable index list.
- **PipelineChain** holds a vector of filters, exactly one serializer, and a chain of encoders. `execute()` applies filters first, then the serializer (which reads only the filtered indices from the snapshot), then passes the raw buffer through each encoder.

### Pipeline Assembly (Factory)

`PipelineFactory::create(SimConfig cfg)` reads configuration flags and instantiates:

- Filters: `AoIFilter` (if enabled), `LodFilter`, `DirtyTrackingFilter`, `LeaderFollowerFilter`, etc.
- Serializer: `FullSnapshotSerializer` (default) or `DeltaSerializer` (if delta enabled). Each serializer may internally quantise floats or apply octahedral heading compression.
- Encoders: `ZstdEncoder`, `HuffmanDictionaryEncoder`, etc.

Because the pipeline is per‑peer, each peer gets its own lightweight chain constructed at the start of its processing (or pre‑built and reused, if per‑peer pipelines are cached).

### Per‑Peer Parallelization

- The `NetPipeline` Flecs system collects all active peers (vector of shared pointers).
- It fans out work to a thread pool (e.g. `BS::thread_pool`). No locks needed: map mutations happen only on the main thread via connection callbacks. Each worker processes one peer independently.
- Steps inside each worker:
    1. Compute `active_indices` (all entities or AoI‑filtered using spatial grid).
    2. Resolve delta baseline from `SnapshotHistory` using `peer->last_acknowledged_tick`.
    3. Construct `PipelineContext`.
    4. Execute the pipeline chain.
    5. Send the final buffer via `INetTransport::send`.

### Flecs Integration

Three new systems, registered in `NetSend` phase (after `SimPost`):

- **`NetPrepareSnapshot`** – iterates all boids post‑integration, copies needed fields into `NetworkSnapshot` (SoA), inserts it into `SnapshotHistory`.
- **`NetComputePeerVisibility`** – for each peer, queries the spatial grid (same grid used for neighbour search) and fills `peer->visible_indices`. If AoI disabled, just a `std::iota`.
- **`NetPipeline`** – dispatches per‑peer pipeline execution as described. Calls into transport.

All systems communicate via world singletons and the `NetManager` context. `NetManager::broadcast_snapshot` is replaced by a `send_snapshot_per_peer` method that receives the snapshot, peer state, and index list, then runs the pipeline.

### Client‑Side Reversal

On the client, each received snapshot packet goes through a reverse chain:

1. **Post‑decode** (Zstd decompress, Huffman decode).
2. **Deserialize** – read flags, reconstruct state from delta or full baselines. Writes directly into Flecs tables (position, heading, hue) to avoid intermediate allocations.
3. **Dequantise** / **unmap octahedral heading** if applicable.

A `ClientPipeline` class encapsulates this order, mirroring the server’s pipeline but in reverse.

### Transport Abstraction

- The pipeline produces a `NetBuffer` (big‑endian byte stream). `INetTransport::send` is the only interface to ENet.
- No raw `ENetPeer*` stored in peer state; we use `PeerID` and the transport interface.
- The transport layer remains pluggable for future TCM or other protocols.
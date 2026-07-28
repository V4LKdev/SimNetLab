# simnet_game_server

`simnet_game_server` owns authoritative server-side Flecs lifecycle, boid simulation, and snapshot extraction.

Its first responsibility is narrow:

```txt
authoritative Flecs components
-> immutable CurrentState SoA
-> simulation spatial grid
-> disjoint NextState rows
-> validated Flecs commit
-> validated WorldSnapshot
```

Allowed dependencies:

```txt
simnet_core
simnet_snapshot
simnet_game_shared
simnet_spatial (private simulation acceleration)
simnet_telemetry (private implementation tracing only)
Flecs
```

The module must not depend on pipeline, transport, render, synthetic data, config, ENet, or Raylib. App/runtime code maps shared configuration into the renderer-independent simulation settings.

`ServerGameRuntime` owns Server-private velocity and precise hue state, stable row state, reusable SoA buffers, the simulation grid, worker scratch, and one selected-boid diagnostic result. It must outlive its registered Flecs world. `prepare_server_game_runtime` sizes all external storage before `world.progress()`.

Explicit Flecs phases run authoritative player movement before boid capture, then serial grid build, multithreaded compute, serial validation/merge, and multithreaded commit. Compute reads only immutable previous-tick boid state and writes `NextState[FlockRow]`. A worker never mutates Flecs components, resizes runtime storage, shares neighbour arrays, or updates shared counters. Invalid next state skips the complete commit.

Authoritative boid creation and deletion must use this module's mutation API. Systems may update boid components directly, but they must not create or delete indexed boids behind the module's private replication index. The index keeps stable network IDs and Flecs entity handles in ascending ID order.

`append_authoritative_boids` accepts only a strictly ascending batch of new IDs. Call it on the world-owning thread outside Flecs iteration and deferred mutation contexts. Active observers must not create or delete boids during the bulk call because Flecs returns transient entity IDs that SimNet copies immediately after insertion. This supports startup population and later load-ramp additions without repeated world scans.

Extraction gathers authoritative entities with `NetIdentity`, `Position`, `Heading`, and `Hue`, sorts by `EntityNetId`, validates the resulting `WorldSnapshot`, and does not mutate the Flecs world. On extraction failure, the output snapshot is cleared and its tick is set to the requested tick.

`EntityKind` distinguishes Boids from the one supported session-owned Player without changing the snapshot record. The Player is included in extraction but has no `FlockRow`, velocity SoA row, or neighbour-rule work. Its latest semantic input is replaced atomically on the owner thread, then a single-threaded Flecs system accelerates private yaw/pitch velocities, damps released axes, enforces angular-rate and pitch limits, targets speed smoothly, and integrates the authoritative pose. Paused worlds do not advance or accumulate player motion. Session disconnect deletes the Player through the indexed authoritative lifecycle.

Velocity, precise hue phase, deterministic wander inputs, row mapping, neighbour scratch, and rule diagnostics are Server-private and never enter the snapshot wire contract. Wander is a smooth stateless function of run seed, stable ID, and tick, so worker scheduling cannot change its result. Hue assimilation averages accepted neighbour hues on the unit circle; isolated hue drift uses a stable per-ID target. Only the quantized authoritative hue component enters snapshots.

Each steering and hue rule has a direct settings toggle. Alignment and cohesion use independent radii, while the simulation grid query uses the maximum separation/alignment/cohesion radius. The selected-boid record exposes wander and hue decisions without retaining debug arrays for every entity.

Snapshot extraction remains world-query based for now. The private sorted index is a future profiling candidate for direct ordered extraction, but that optimization is intentionally separate from simulation.

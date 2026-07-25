# simnet_game_server

`simnet_game_server` owns authoritative server-side Flecs world lifecycle and snapshot extraction.

Its first responsibility is narrow:

```txt
authoritative Flecs world -> validated WorldSnapshot
```

Allowed dependencies:

```txt
simnet_core
simnet_snapshot
simnet_game_shared
simnet_telemetry (private implementation tracing only)
Flecs
```

The module must not depend on pipeline, transport, render, synthetic data, spatial indexing, config, ENet, or Raylib. App/runtime code can combine those layers later.

Authoritative boid creation and deletion must use this module's mutation API. Systems may update boid components directly, but they must not create or delete indexed boids behind the module's private replication index. The index keeps stable network IDs and Flecs entity handles in ascending ID order.

`append_authoritative_boids` accepts only a strictly ascending batch of new IDs. Call it on the world-owning thread outside Flecs iteration and deferred mutation contexts. Active observers must not create or delete boids during the bulk call because Flecs returns transient entity IDs that SimNet copies immediately after insertion. This supports startup population and later load-ramp additions without repeated world scans.

Extraction gathers authoritative boid entities with `NetIdentity`, `Position`, `Heading`, `Hue`, and `BoidTag`, sorts by `EntityNetId`, validates the resulting `WorldSnapshot`, and does not mutate the Flecs world. On extraction failure, the output snapshot is cleared and its tick is set to the requested tick.

Snapshot extraction remains world-query based for now. The private sorted index is a future profiling candidate for direct ordered extraction, but that optimization is intentionally separate from authoritative population.

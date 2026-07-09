# simnet_game_server

`simnet_game_server` owns authoritative server-side Flecs world extraction.

Its first responsibility is narrow:

```txt
authoritative Flecs world -> validated WorldSnapshot
```

Allowed dependencies:

```txt
simnet_core
simnet_snapshot
simnet_game_shared
Flecs
```

The module must not depend on pipeline, transport, render, synthetic data, spatial indexing, telemetry, config, ENet, or Raylib. App/runtime code can combine those layers later.

Extraction gathers authoritative boid entities with `NetIdentity`, `Position`, `Heading`, `Hue`, and `BoidTag`, sorts by `EntityNetId`, validates the resulting `WorldSnapshot`, and does not mutate the Flecs world. On extraction failure, the output snapshot is cleared and its tick is set to the requested tick.

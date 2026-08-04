# simnet_synthetic

`simnet_synthetic` creates deterministic workload snapshots for pipeline, rendering, and benchmark bring-up.

It produces valid `WorldSnapshot` values without depending on ECS, game simulation, transport, render, telemetry, benchmarking, config, or pipeline code. All generated entities use the explicit opaque classification value 1, which matches the primary homogeneous workload while keeping synthetic generation independent from Flecs and game vocabulary. The first pass supports grid and seeded random-uniform placement. Generated snapshots are stable for the same settings and tick.

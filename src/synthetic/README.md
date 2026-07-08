# simnet_synthetic

`simnet_synthetic` creates deterministic workload snapshots for pipeline, rendering, and benchmark bring-up.

It produces valid `WorldSnapshot` values without depending on ECS, game simulation, transport, render, telemetry, benchmarking, config, or pipeline code. The first pass supports grid and seeded random-uniform placement. Generated snapshots are stable for the same settings and tick.

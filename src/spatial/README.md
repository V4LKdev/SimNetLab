# simnet_spatial

`simnet_spatial` is a reusable bounded sparse sorted uniform-grid acceleration module.

It stores sorted source indices into external SoA position data and exposes immutable candidate queries for simulation, AoI, and LOD systems. It does not own boid behavior, ECS state, snapshots, telemetry, transport, rendering, or network replication policy.

The serial rebuild path computes bounded cell keys, sorts source indices by occupied cell, compacts cell ranges, and records occupancy stats. Queries are allocation-free, read-only, and exact-filter positions after enumerating overlapping cells. The parallel build entry point delegates to the serial path until a measured parallel backend is added.

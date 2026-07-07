# simnet_spatial

`simnet_spatial` is a reusable bounded sparse sorted uniform-grid acceleration module.

It stores sorted source indices into external SoA position data and exposes immutable candidate queries for simulation, AoI, and LOD systems. It does not own boid behavior, ECS state, snapshots, telemetry, transport, rendering, or network replication policy.

The rebuild API is phased: resize when settings change, prepare scratch for capacity and worker count, begin a build, let external workers fill their own entry buffers, then finish with a single-threaded merge, sort, compact, and stats pass. The serial helper wraps that flow with one worker. Queries are allocation-free, read-only, and exact-filter positions after enumerating overlapping cells.

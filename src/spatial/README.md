# simnet_spatial

`simnet_spatial` is a reusable bounded sparse sorted uniform-grid acceleration module.

It stores sorted source indices into external SoA position data and exposes immutable candidate queries for simulation, AoI, and LOD systems. It does not own boid behavior, ECS state, snapshots, telemetry, transport, rendering, or network replication policy.

The rebuild API is phased: resize when settings change, prepare scratch for capacity and worker count, begin a build, let external workers fill their own entry buffers, then finish with a single-threaded merge, sort, compact, and stats pass.

The phased path sorts merged entries by cell key and source index. Worker count and shard completion
order do not change the committed grid output.

The serial helper uses a deterministic counting/bucket build for bounded grids of at most 262,144 cells. It falls back to comparison sorting for larger valid grids. An ID-aware overload orders entries within each cell by `EntityNetId`. The positions-only overload preserves source-index ordering. Both paths stage their result in reusable scratch storage and commit only after validation and construction succeed.

Queries are allocation-free, read-only, and exact-filter positions after enumerating overlapping cells.

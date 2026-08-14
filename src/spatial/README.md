# simnet_spatial

`simnet_spatial` is a reusable bounded sparse sorted uniform-grid acceleration module.

It indexes external structure-of-arrays position data without owning those arrays.

Rebuilds are staged. Callers prepare reusable worker-local scratch, run shard builds, then commit only after validation and grid construction succeed. Committed output ordering is deterministic regardless of worker count or shard-completion order.

Queries are allocation-free and read-only. Radius and AABB boundaries are inclusive, and exact position filtering runs after overlapping-cell enumeration.

`simnet_spatial` depends on `simnet_core` and owns no gameplay, ECS, AOI policy, rendering, transport, or replication policy.

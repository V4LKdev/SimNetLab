# simnet_spatial

`simnet_spatial` provides a bounded sparse uniform grid over caller-owned structure-of-arrays
position data.

Rebuilds prepare worker-local scratch, build independent shards, then commit after validation and
grid construction succeed. Committed ordering is deterministic for every worker count and shard
completion order.

Queries are allocation-free and read-only. Radius and AABB boundaries are inclusive, with exact
position filtering after overlapping-cell enumeration.

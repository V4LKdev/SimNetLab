# simnet_game_client

`simnet_game_client` owns client-side Flecs replication contracts.

It registers shared game components plus client replication indexing state. Patch application consumes decoded `ClientSnapshotPatch` data and updates a Flecs client world.

The module depends only on `simnet_game_shared` and `simnet_snapshot`. It does not own pipeline decoding, transport, rendering, telemetry, config, synthetic data, or server simulation.

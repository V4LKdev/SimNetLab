# simnet_game_client

`simnet_game_client` owns the nonauthoritative Client Flecs replicated sink.

It registers shared game components plus private sink application state. Call `register_client_game` once during Client setup. `apply_client_snapshot_patch` consumes a decoded or reconstructed `SnapshotUpdate` and applies it to the Flecs sink. It maps classifications 1 and 2 back to Boid and Player component values. Unsupported nonzero classifications and updates older than the latest accepted sink tick are rejected before mutating entities or sink application state. Equal ticks are accepted for future multi-packet ticks.

The Client application retains successfully reconstructed `WorldSnapshot` history as its canonical replicated representation. That history owns exact delta baselines, the latest replicated state, presentation interpolation, rendering data, stable-ID selection, and camera lookup. Rendering remains independent of Flecs.

Applying each successfully reconstructed update to the Flecs sink is a required full-system ECS workload. Sink application must succeed before the Client advances its applied sequence or acknowledges the update as applied. Full-system experiments include and separately measure this application cost. Pipeline-only experiments exclude the sink.

The Flecs sink is never authoritative and never replaces retained reconstructed snapshots. Local player ownership comes from the `JoinAccepted` player ID, not Flecs classification. Generic `EntityClassification` values map to `EntityKindComponent`, so multiple remote Player entities remain represented independently of local ownership.

The module depends only on `simnet_game_shared` and `simnet_snapshot`. It does not own pipeline decoding, transport, rendering, telemetry, config, synthetic data, or server simulation.

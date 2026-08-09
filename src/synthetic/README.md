# simnet_synthetic

`simnet_synthetic` creates deterministic workload snapshots for pipeline, rendering, and research runs.

It produces valid `WorldSnapshot` values without depending on ECS, game simulation, transport, render, telemetry, config, or pipeline code. All generated entities use the explicit opaque classification value 1, which matches the primary homogeneous workload while keeping synthetic generation independent from Flecs and game vocabulary. Grid and seeded random-uniform placement are stable for the same settings and tick.

`SyntheticSnapshotState` owns the retained current snapshot, reusable candidate storage, cohort cursor, accepted settings, and sequence state. The first supplied tick initializes a complete snapshot. Later calls must use the same settings and exactly the next tick. A fraction of zero retains every entity, a positive fraction services at least one entity, and other fractions service `floor(entity_count * fraction)` entities up to the complete population. The cohort rotates through ascending `EntityNetId` order.

The field modes are `all`, `transform`, `position_only`, and `heading_only`. All changes position, heading, and hue. Transform changes position and heading. The two narrow modes change only their named field. IDs, classifications, and population membership remain stable in every mode. Entities outside the active cohort retain their exact canonical field bits.

The Server selects this producer only when the shared `synthetic` section is present. In that mode it does not construct or step the Flecs authoritative world. Server visualization and Player clients are rejected because both require that world. Stationary observer clients consume the ordinary pipeline and can render the reconstructed synthetic snapshot.

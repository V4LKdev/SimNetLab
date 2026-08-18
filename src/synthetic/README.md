# simnet_synthetic

`simnet_synthetic` creates deterministic `WorldSnapshot` workloads without running the Flecs
simulation.

Grid and seeded random-uniform placement are stable for the same settings and tick. The first tick
creates a complete snapshot. Later ticks must be consecutive and use the same settings. The active
cohort rotates through ascending `EntityNetId` order, while entities outside it keep their exact
canonical field bits.

Field modes are `all`, `transform`, `position_only`, and `heading_only`. Population membership,
identifiers, and classifications remain stable in every mode. `all` changes position, heading, and
hue. `transform` changes position and heading. The two narrow modes change only their named field.

A zero change fraction keeps every entity unchanged. A positive fraction services at least one
entity and otherwise uses `floor(entity_count * fraction)` up to the complete population.

The Server selects the synthetic producer when the shared configuration contains `synthetic`.
Synthetic mode uses the ordinary replication pipeline, but rejects Server rendering and Player
clients because those features require the authoritative Flecs world.

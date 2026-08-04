# simnet_game_shared

`simnet_game_shared` defines the shared Flecs component vocabulary for Server and Client game worlds.

It owns replicated identity, position, heading, and hue component contracts. It also owns the mapping from `EntityKind::Boid` to generic classification 1 and `EntityKind::Player` to generic classification 2. It depends on `simnet_snapshot` for the opaque classification type without creating a snapshot-to-game dependency. It does not own Server-private velocity, pipeline encoding, transport, rendering, telemetry, config, or simulation policy.

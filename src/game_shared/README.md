# simnet_game_shared

`simnet_game_shared` defines the shared Flecs component vocabulary for server and client game worlds.

It owns replicated identity, position, heading, and hue component contracts. It does not own Server-private velocity, snapshots, pipeline encoding, transport, rendering, telemetry, config, or simulation policy.

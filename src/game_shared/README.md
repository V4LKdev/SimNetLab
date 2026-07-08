# simnet_game_shared

`simnet_game_shared` defines the shared Flecs component vocabulary for server and client game worlds.

It owns replicated identity, position, heading, hue, and boid marker component contracts. It does not own snapshots, pipeline encoding, transport, rendering, telemetry, config, or simulation policy.

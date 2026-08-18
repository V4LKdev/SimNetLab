# simnet_game_shared

`simnet_game_shared` defines the Flecs components shared by the Server and Client.

It contains replicated identity, position, heading, hue, and the mapping from game entity types to
snapshot classifications. Boids use classification 1 and Players use classification 2.

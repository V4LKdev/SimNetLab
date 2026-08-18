# simnet_game_client

`simnet_game_client` applies reconstructed replicated state to the nonauthoritative Client Flecs
world.

Call `register_client_game` once during setup. `apply_client_snapshot_patch_unchecked` accepts a
previously validated `SnapshotUpdate`, maps snapshot classifications to Boid and Player components,
and applies the update to Flecs. Unsupported nonzero classifications and updates older than the
latest accepted sink tick are rejected before mutation. Equal ticks remain valid.

The Client application keeps reconstructed `WorldSnapshot` history as its canonical replicated
state. Sink application must succeed before the applied sequence advances or an ACK confirms the
update. This makes Flecs application part of full-system measurements without using the sink as a
Delta baseline.

Local Player ownership comes from the player ID in `JoinAccepted`, not from entity classification.
Multiple remote Players can therefore remain represented independently of local ownership.

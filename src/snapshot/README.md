# simnet_snapshot

`simnet_snapshot` owns the public replicated state contract for one simulation tick.

WorldSnapshot is canonical simulation truth; ClientSnapshotPatch is logical client change; neither is encoded network representation.

`WorldSnapshot` uses SoA vectors and requires strictly ascending entity IDs. `ClientSnapshotPatch` uses sorted upserts and deletes. Validation returns the first clear error message and checks sizes, ordering, finite vectors, normalized headings, and duplicate patch membership.

This module does not know about pipeline stages, transport packets, spatial queries, telemetry, rendering, or client storage.

@defgroup snapshot simnet.snapshot
@brief Public replicated state contract for one simulation tick.

## Exported Types

### simnet.snapshot:types
- `EntityState` - generic per-entity replicated state (id, position, heading, hue).
- `WorldSnapshot` - SoA representation of full world state.
- `SnapshotUpdate` - generic full or partial state update (upserts, deletes, kind).
- `SnapshotKind` - FullReplace or Patch.
- `SnapshotValidationResult` - boolean valid and error message.

### simnet.snapshot:validate
- `is_normalized_heading` - tolerance-based heading length check.
- `validate_world_snapshot` - validates sizes, id ordering, finite vectors, normalized headings.
- `validate_client_snapshot_patch` - same for patches, plus no id in both upserts and deletes.

### simnet.snapshot:reconstruct
- `reconstruct_world_snapshot` - transactionally creates a complete snapshot from a full replacement or an exact retained baseline plus a patch.

### simnet.snapshot:interpolate
- `interpolate_world_snapshots` - builds a reusable presentation snapshot from two validated complete snapshots.

## Notes
- Both `WorldSnapshot` and `SnapshotUpdate` require strictly ascending entity IDs.
- Validation returns the first contract violation. Error messages are heap-allocated.
- `WorldSnapshot` uses SoA layout for cache efficiency.
- `SnapshotUpdate::clear` preserves the `kind` field. Tick is reset to zero.
- Heading normalization tolerance is `0.01F` (defined in types).
- Delta reconstruction is storage-independent. The caller owns retained snapshots and resolves the sequence declared by the decoded packet.
- Presentation interpolation uses the current entity set, normalized heading interpolation, and circular hue interpolation. It does not mutate either authoritative input.

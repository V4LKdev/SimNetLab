@defgroup snapshot simnet.snapshot
@brief Public replicated state contract for one simulation tick.

## Exported Types

### simnet.snapshot:types
- `BoidState` - per-entity state (id, position, heading, hue).
- `WorldSnapshot` - SoA representation of full world state.
- `ClientSnapshotPatch` - logical client change (upserts, deletes, kind).
- `SnapshotKind` - FullReplace or Patch.
- `SnapshotValidationResult` - boolean valid and error message.

### simnet.snapshot:validate
- `is_normalized_heading` - tolerance-based heading length check.
- `validate_world_snapshot` - validates sizes, id ordering, finite vectors, normalized headings.
- `validate_client_snapshot_patch` - same for patches, plus no id in both upserts and deletes.

## Notes
- Both `WorldSnapshot` and `ClientSnapshotPatch` require strictly ascending entity IDs.
- Validation returns the first contract violation. Error messages are heap-allocated.
- `WorldSnapshot` uses SoA layout for cache efficiency.
- `ClientSnapshotPatch::clear` preserves the `kind` field. Tick is reset to zero.
- Heading normalization tolerance is `0.01F` (defined in types).

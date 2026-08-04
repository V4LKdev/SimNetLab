@defgroup snapshot simnet.snapshot
@brief Public replicated state contract for one simulation tick.

## Exported Types

### simnet.snapshot:types
- `EntityClassification` - strong one-byte opaque classification with zero reserved as invalid.
- `EntityState` - generic per-entity replicated state.
- `WorldSnapshot` - SoA representation of full world state.
- `SnapshotUpdate` - generic full or partial state update (upserts, deletes, kind).
- `SnapshotKind` - FullReplace or Patch.
- `SnapshotValidationResult` - boolean valid and error message.

### simnet.snapshot:validate
- `is_normalized_heading` - tolerance-based heading length check.
- `validate_world_snapshot` - validates SoA sizes, nonzero classifications, nonzero ordered ids, finite vectors, and normalized headings.
- `validate_client_snapshot_patch` - validates the same entity rules for upserts and deletes, plus no id in both lists.

### simnet.snapshot:reconstruct
- `reconstruct_world_snapshot` - transactionally creates a complete snapshot from a full replacement or an exact retained baseline plus a patch.

### simnet.snapshot:interpolate
- `interpolate_world_snapshots` - builds a reusable presentation snapshot from two validated complete snapshots. Categorical classification always uses the current authoritative endpoint.

## Notes
- Both `WorldSnapshot` and `SnapshotUpdate` reserve entity ID zero and require strictly ascending IDs.
- Classification zero is invalid. Generic snapshot code preserves every unknown nonzero value.
- Validation returns the first contract violation. Error messages are heap-allocated.
- `WorldSnapshot` uses SoA layout for cache efficiency.
- `SnapshotUpdate::clear` preserves the `kind` field. Tick is reset to zero.
- Heading normalization tolerance is `0.01F` (defined in types).
- Delta reconstruction is storage-independent. The caller owns retained snapshots and resolves the sequence declared by the decoded packet.
- Presentation interpolation uses the current entity set, normalized heading interpolation, and circular hue interpolation. It does not mutate either authoritative input.

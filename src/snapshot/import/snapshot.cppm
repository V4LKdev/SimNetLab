/**
* @defgroup snapshot simnet.snapshot
 * @brief Public replicated state contract for one simulation tick.
 *
 * @details
 * Defines the authoritative world state (`WorldSnapshot`) and client-side
 * logical changes (`ClientSnapshotPatch`). These are raw data contracts.
 * They are not wire formats and know nothing about transport or encoding.
 *
 * @note All types use standard containers. Validation functions allocate
 * strings only on error.
 */
export module simnet.snapshot;

/**
 * @par simnet.snapshot:types
 * Core snapshot types: `BoidState`, `WorldSnapshot` (SoA layout),
 * `ClientSnapshotPatch`, `SnapshotKind`, and `SnapshotValidationResult`.
 */
export import :types;

/**
 * @par simnet.snapshot:validate
 * Validation routines that enforce the snapshot contract: vector sizes,
 * ID ordering, finite positions, normalized headings, and non-overlapping
 * upsert/delete sets.
 */
export import :validate;

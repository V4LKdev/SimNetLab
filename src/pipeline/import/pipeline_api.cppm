module;

#include <cstdint>

/// @brief Pipeline encode and decode API.
export module simnet.pipeline:api;

import :types;
import :messages;

export namespace simnet
{
    /**
     * Validates a supported pipeline definition and its settings.
     *
     * Active techniques compose unless one requires another representation technique.
     * OctHeading requires Quantization. BitPacking requires Quantization and OctHeading.
     */
    void validate_pipeline_definition(PipelineDefinition const& pipeline);

    /**
     * Returns whether the authoritative snapshot tick is eligible for encoding.
     *
     * Disabled cadence always emits. Enabled cadence requires a positive interval and emits only
     * when the tick is divisible by it. This query does not mutate pipeline state or scratch.
     */
    [[nodiscard]] bool should_emit_snapshot(PipelineDefinition const& pipeline, Tick tick);

    /// Computes the canonical decode-representation signature for the given pipeline.
    [[nodiscard]] std::uint64_t
    pipeline_decode_signature(PipelineDefinition const& definition) noexcept;

    /**
     * Measures source-to-canonical error for the upserts produced by one successful encode.
     *
     * `source_snapshot` must be the authoritative snapshot supplied to that encode and
     * `represented_update` must be its pipeline-owned logical update. The operation does not
     * mutate pipeline state or encoded bytes.
     */
    [[nodiscard]] RepresentationReport measure_representation_quality(
        PipelineDefinition const& pipeline,
        WorldSnapshot const& source_snapshot,
        SnapshotUpdate const& represented_update
    );

    /**
     * Inspects and validates the fixed encoded update header without allocation or mutation.
     *
     * This validates schema, signature, sequence admissibility, declared payload size, snapshot
     * and baseline relationships, and safe widened payload bounds. Variable field-mask records
     * remain subject to complete validation by decode_update or decode_update_unchecked.
     */
    [[nodiscard]] EncodedUpdateHeaderInspection inspect_encoded_update_header(
        PipelineDefinition const& pipeline,
        ClientReplicationState const& client_state,
        ByteSpan bytes
    ) noexcept;

    /**
     * Encodes an authoritative snapshot into a pipeline-owned encoded update.
     *
     * This is the normal entry point for arbitrary snapshot values. It validates the current and
     * optional baseline entity contracts before delegating to encode_snapshot_unchecked.
     *
     * - Reads settings from 'pipeline'.
     * - Mutates 'client_state'.
     * - Reuses 'scratch' internal buffers.
     * - Returns the exact complete Client result represented by every emitted update.
     * - Returns a skipped result only when SendInterval rejects the current tick.
     */
    [[nodiscard]] EncodeOutput encode_snapshot(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        EncodeInput const& input
    );

    /**
     * Advanced encode path for caller-owned snapshots with proven validity.
     *
     * The current snapshot and optional baseline must satisfy validate_world_snapshot. Successful
     * authoritative extraction or another checked producer boundary must establish that contract.
     * Retained baselines must remain under ownership that preserves the invariant. The caller must
     * not mutate either snapshot between that proof and this call. This function validates the
     * pipeline definition, rejects a null current pointer, validates baseline presence, sequence,
     * and technique relationships before changing client state or scratch, and preserves client
     * state when later sequence or size checks reject. Arbitrary or external snapshot values must
     * use encode_snapshot.
     */
    [[nodiscard]] EncodeOutput encode_snapshot_unchecked(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        EncodeInput const& input
    );

    /**
     * Decodes pipeline-owned bytes into a 'SnapshotUpdate'.
     *
     * - Validates the wire header and sequence numbers against 'client_state'.
     * - Returns a 'DecodeOutput' with either a valid update or error report.
     */
    [[nodiscard]] DecodeOutput decode_update(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        DecodeInput const& input
    );

    /**
     * Decodes external bytes while trusting only the supplied retained baseline invariant.
     *
     * The baseline must come from successful reconstruction and application, remain under
     * invariant-preserving ownership, and not mutate between that proof and this call. All wire
     * bytes, masks, records, ordering, and update semantics are validated exactly as in
     * decode_update. Arbitrary caller-provided baselines must use decode_update.
     */
    [[nodiscard]] DecodeOutput decode_update_unchecked(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        DecodeInput const& input
    );
}

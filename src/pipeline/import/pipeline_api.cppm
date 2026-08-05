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

    /// Computes the canonical decode-representation signature for the given pipeline.
    [[nodiscard]] std::uint64_t
    pipeline_decode_signature(PipelineDefinition const& definition) noexcept;

    /**
     * Encodes an authoritative snapshot into a pipeline-owned encoded update.
     *
     * This is the normal entry point for arbitrary snapshot values. It validates the current and
     * optional baseline entity contracts before delegating to encode_snapshot_unchecked.
     *
     * - Reads settings from 'pipeline'.
     * - Mutates 'client_state'.
     * - Reuses 'scratch' internal buffers.
     * - Returns 'EncodeOutput' with either an encoded update or skipped result.
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
}

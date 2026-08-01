module;

#include <cstdint>

/// @brief Pipeline encode and decode API.
export module simnet.pipeline:api;

import :types;
import :messages;

export namespace simnet
{
    /// Validates a supported pipeline definition and its settings.
    void validate_pipeline_definition(PipelineDefinition const& pipeline);

    /// Computes the canonical decode-representation signature for the given pipeline.
    [[nodiscard]] std::uint64_t
    pipeline_decode_signature(PipelineDefinition const& definition) noexcept;

    /**
     * Encodes an authoritative snapshot into a pipeline-owned encoded update.
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
     * Decodes pipeline-owned bytes into a 'SnapshotUpdate'.
     *
     * - Validates the wire header and sequence numbers against 'client_state'.
     * - Reuses 'scratch' internal buffers.
     * - Returns a 'DecodeOutput' with either a valid update or error report.
     */
    [[nodiscard]] DecodeOutput decode_update(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        DecodeInput const& input
    );
}

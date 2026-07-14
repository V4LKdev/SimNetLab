module;

#include <cstdint>

/// @brief Pipeline encode and decode API.
export module simnet.pipeline:codec;

import :types;
import :messages;

export namespace simnet
{
    /// Factory: builds a `PipelineDefinition` preset to raw byte-aligned snapshot.
    [[nodiscard]] PipelineDefinition make_raw_snapshot_pipeline(PacketBudget budget = {});

    /// Computes the canonical decode-representation signature for the given pipeline.
    [[nodiscard]] std::uint64_t pipeline_decode_signature(
        PipelineDefinition const& definition
    ) noexcept;

    /**
     * Encodes an authoritative snapshot into a pipeline-owned packet.
     *
     * - Reads settings from 'pipeline'.
     * - Mutates 'client_state'.
     * - Reuses 'scratch' internal buffers.
     * - Returns 'EncodeOutput' with either a packet or skipped result.
     */
    [[nodiscard]] EncodeOutput encode_snapshot(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        EncodeInput const& input
    );

    /**
     * Decodes a pipeline-owned packet into a 'ClientSnapshotPatch'.
     *
     * - Validates the wire header and sequence numbers against 'client_state'.
     * - Reuses 'scratch' internal buffers.
     * - Returns a 'DecodeOutput' with either valid patch or error report.
     */
    [[nodiscard]] DecodeOutput decode_packet(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        DecodeInput const& input
    );
}

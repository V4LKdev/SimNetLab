/// @brief Pipeline encode and decode API.
export module simnet.pipeline:codec;

import :types;

export namespace simnet
{
    /// Creates the raw byte-aligned snapshot pipeline profile.
    [[nodiscard]] PipelineDefinition make_raw_snapshot_pipeline(PacketBudget budget = {});

    /// Encodes an authoritative snapshot into a pipeline-owned packet.
    [[nodiscard]] EncodeOutput encode_snapshot(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        EncodeInput const& input
    );

    /// Decodes a pipeline-owned packet into a logical client patch.
    [[nodiscard]] DecodeOutput decode_packet(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        DecodeInput const& input
    );
}

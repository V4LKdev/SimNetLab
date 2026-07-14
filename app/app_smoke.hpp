#pragma once

namespace simnet::app
{
    inline constexpr std::uint32_t application_protocol_version = 1;
    inline constexpr std::uint32_t smoke_boid_count = 10;
    inline constexpr Tick smoke_tick_count = 5;
    inline constexpr Tick smoke_final_tick = smoke_tick_count - 1;
    inline constexpr SequenceId smoke_final_sequence = 3;
    inline constexpr std::uint32_t smoke_final_received_mask = 0x3U;

    [[nodiscard]] inline PipelineDefinition make_smoke_pipeline(SharedConfig const& shared_config)
    {
        auto pipeline = make_raw_snapshot_pipeline();
        pipeline.techniques |= PipelineTechniqueFlags::SendInterval;
        pipeline.techniques |= PipelineTechniqueFlags::Quantization;
        pipeline.techniques |= PipelineTechniqueFlags::OctHeading;
        pipeline.techniques |= PipelineTechniqueFlags::BitPacking;
        pipeline.send_interval.interval_ticks = 2;
        pipeline.quantization.position_bounds = make_centered_bounds(shared_config.simulation.world_half);
        return pipeline;
    }

    [[nodiscard]] inline SessionIdentity make_session_identity(
        SharedConfig const& shared_config,
        PipelineDefinition const& pipeline
    )
    {
        return {
            .application_protocol_version = application_protocol_version,
            .compatibility_fingerprint = fingerprint_network_compatibility(shared_config).value,
            .pipeline_decode_signature = pipeline_decode_signature(pipeline),
            .capabilities = 0,
        };
    }
}

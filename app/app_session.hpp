#pragma once

namespace simnet::app
{
    inline constexpr std::uint32_t application_protocol_version = 1;

    [[nodiscard]] inline PipelineDefinition make_smoke_pipeline(SharedConfig const& shared_config)
    {
        auto pipeline = make_raw_snapshot_pipeline();
        pipeline.codec = CodecKind::BitPacked;
        pipeline.techniques |= PipelineTechniqueFlags::SendInterval;
        pipeline.techniques |= PipelineTechniqueFlags::Quantization;
        pipeline.techniques |= PipelineTechniqueFlags::OctHeading;
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

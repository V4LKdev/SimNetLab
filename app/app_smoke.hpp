#pragma once

#include <stdexcept>
#include <string>

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
        pipeline.techniques |= PipelineTechniqueFlags::Delta;
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

    [[nodiscard]] inline TransportBackend transport_backend(TransportConfig const& config)
    {
        if (config.backend == "enet") {
            return TransportBackend::ENet;
        }
        if (config.backend == "local_ipc") {
            return TransportBackend::LocalIpc;
        }
        throw std::runtime_error("unsupported transport backend: " + config.backend);
    }

    [[nodiscard]] inline SendSizePolicy transport_send_size_policy(TransportConfig const& config)
    {
        if (config.send_size_policy == "enforce_limit") {
            return SendSizePolicy::EnforceLimit;
        }
        if (config.send_size_policy == "allow_backend_fragmentation") {
            return SendSizePolicy::AllowBackendFragmentation;
        }
        throw std::runtime_error("unsupported transport send size policy: " + config.send_size_policy);
    }

    [[nodiscard]] inline Delivery snapshot_delivery(TransportConfig const& config)
    {
        if (config.snapshot_delivery == "reliable_sequenced") {
            return Delivery::ReliableSequenced;
        }
        if (config.snapshot_delivery == "unreliable_sequenced") {
            return Delivery::UnreliableSequenced;
        }
        if (config.snapshot_delivery == "unreliable_unsequenced") {
            return Delivery::UnreliableUnsequenced;
        }
        if (config.snapshot_delivery == "unreliable_fragmented") {
            return Delivery::UnreliableFragmented;
        }
        throw std::runtime_error("unsupported snapshot delivery mode: " + config.snapshot_delivery);
    }

    [[nodiscard]] inline TransportLimits transport_limits(TransportConfig const& config)
    {
        return {
            .max_payload_bytes = config.max_payload_bytes,
            .size_policy = transport_send_size_policy(config),
        };
    }
}

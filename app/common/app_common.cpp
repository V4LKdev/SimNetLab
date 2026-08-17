module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

module simnet.app_common;

import simnet.app_protocol;

namespace
{
    volatile std::sig_atomic_t signal_stop_latch = 0;

    extern "C" void request_signal_stop(int)
    {
        signal_stop_latch = 1;
    }

    [[nodiscard]] simnet::AreaOfInterestSettings
    make_area_of_interest_settings(simnet::AreaOfInterestConfig const& config)
    {
        auto mode = simnet::AreaOfInterestMode::None;
        if (config.mode == "radius")
        {
            mode = simnet::AreaOfInterestMode::Radius;
        }
        else if (config.mode == "fov")
        {
            mode = simnet::AreaOfInterestMode::Fov;
        }
        else if (config.mode != "none")
        {
            throw std::runtime_error("unsupported AOI mode: " + config.mode);
        }
        return {
            .mode = mode,
            .radius = config.radius,
            .fov_degrees = config.fov_degrees,
        };
    }

    [[nodiscard]] simnet::LevelOfDetailSettings
    make_level_of_detail_settings(simnet::LevelOfDetailConfig const& config)
    {
        auto mode = simnet::LevelOfDetailMode::None;
        if (config.mode == "distance_bands")
        {
            mode = simnet::LevelOfDetailMode::DistanceBands;
        }
        else if (config.mode != "none")
        {
            throw std::runtime_error("unsupported level-of-detail mode: " + config.mode);
        }
        return {
            .mode = mode,
            .near_distance = config.near_distance,
            .medium_distance = config.medium_distance,
            .medium_interval_ticks = config.medium_interval_ticks,
            .far_interval_ticks = config.far_interval_ticks,
        };
    }

    void add_enabled_pipeline_techniques(
        simnet::PipelineDefinition& pipeline,
        simnet::SharedConfig const& shared
    )
    {
        if (shared.pipeline.send_interval_ticks > 1U)
        {
            pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval;
            pipeline.send_interval.interval_ticks = shared.pipeline.send_interval_ticks;
        }
        if (shared.pipeline.enable_quantization)
        {
            pipeline.techniques |= simnet::PipelineTechniqueFlags::Quantization;
            pipeline.quantization.position_bounds =
                simnet::make_centered_bounds(shared.simulation.world_half);
        }
        if (shared.pipeline.enable_oct_heading)
        {
            pipeline.techniques |= simnet::PipelineTechniqueFlags::OctHeading;
        }
        if (shared.pipeline.enable_bit_packing)
        {
            pipeline.techniques |= simnet::PipelineTechniqueFlags::BitPacking;
        }
        if (shared.pipeline.enable_incremental)
        {
            pipeline.techniques |= simnet::PipelineTechniqueFlags::Incremental;
        }
        if (shared.pipeline.enable_delta)
        {
            pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
        }
        if (shared.pipeline.enable_delta_field_mask)
        {
            pipeline.techniques |= simnet::PipelineTechniqueFlags::DeltaFieldMask;
        }
    }
}

namespace simnet::app
{
    Vec3f stationary_observer_forward(StationaryObserverState const& state) noexcept
    {
        auto const cosine_pitch = std::cos(state.pitch);
        return {
            .x = cosine_pitch * std::sin(state.yaw),
            .y = std::sin(state.pitch),
            .z = cosine_pitch * std::cos(state.yaw),
        };
    }

    void apply_stationary_observer_rotation(
        StationaryObserverState& state,
        float yaw_axis,
        float pitch_axis,
        Nanoseconds frame_delta
    ) noexcept
    {
        auto const seconds = std::chrono::duration<float>(frame_delta).count();
        auto constexpr rotation_speed = 1.5F;
        auto constexpr pitch_limit = 1.483529864F;
        state.yaw += yaw_axis * rotation_speed * seconds;
        state.pitch = std::clamp(
            state.pitch + pitch_axis * rotation_speed * seconds,
            -pitch_limit,
            pitch_limit
        );
    }

    SignalHandlers::SignalHandlers()
        : interrupt_(std::signal(SIGINT, request_signal_stop)),
          terminate_(std::signal(SIGTERM, request_signal_stop))
    {
    }

    SignalHandlers::~SignalHandlers()
    {
        std::signal(SIGINT, interrupt_);
        std::signal(SIGTERM, terminate_);
    }

    TelemetryLifetime::TelemetryLifetime(TelemetryConfig const& config)
    {
        initialize_telemetry(config);
    }

    TelemetryLifetime::~TelemetryLifetime() noexcept
    {
        try
        {
            shutdown();
        }
        catch (...)
        {
            return;
        }
    }

    void TelemetryLifetime::shutdown()
    {
        if (!active_)
        {
            return;
        }
        shutdown_telemetry();
        active_ = false;
    }

    bool signal_stop_requested() noexcept
    {
        return signal_stop_latch != 0;
    }

    std::string_view next_option_value(int& index, int argc, char** argv, std::string_view option)
    {
        if (++index >= argc)
        {
            throw std::runtime_error("missing value for " + std::string{option});
        }
        return argv[index];
    }

    std::chrono::milliseconds
    milliseconds_option(int& index, int argc, char** argv, std::string_view option)
    {
        auto const value =
            parse_unsigned<std::uint64_t>(next_option_value(index, argc, argv, option), option);
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            throw std::runtime_error("value out of range for " + std::string{option});
        }
        return std::chrono::milliseconds{static_cast<std::int64_t>(value)};
    }

    SendSizePolicy transport_send_size_policy(TransportConfig const& config)
    {
        if (config.send_size_policy == "enforce_limit")
        {
            return SendSizePolicy::EnforceLimit;
        }
        if (config.send_size_policy == "allow_backend_fragmentation")
        {
            return SendSizePolicy::AllowBackendFragmentation;
        }
        throw std::runtime_error("unsupported send size policy: " + config.send_size_policy);
    }

    TransportDelivery snapshot_transport_delivery(SnapshotDeliveryConfig const& config)
    {
        if (config.mode == "reliable_sequenced")
        {
            return TransportDelivery::ReliableSequenced;
        }
        if (config.mode == "unreliable_sequenced")
        {
            return TransportDelivery::UnreliableSequenced;
        }
        throw std::runtime_error("unsupported snapshot delivery: " + config.mode);
    }

    PipelineDefinition make_snapshot_pipeline(SharedConfig const& shared)
    {
        auto pipeline = PipelineDefinition{};
        add_enabled_pipeline_techniques(pipeline, shared);
        pipeline.area_of_interest =
            make_area_of_interest_settings(shared.pipeline.area_of_interest);
        pipeline.level_of_detail = make_level_of_detail_settings(shared.pipeline.level_of_detail);
        validate_pipeline_definition(pipeline);
        return pipeline;
    }

    CompressionSettings make_compression_settings(SharedConfig const& shared)
    {
        auto mode = CompressionMode::None;
        if (shared.compression.mode == "whole_update")
        {
            mode = CompressionMode::WholeUpdate;
        }
        else if (shared.compression.mode == "per_packet")
        {
            mode = CompressionMode::PerPacket;
        }
        else if (shared.compression.mode != "none")
        {
            throw std::runtime_error("unsupported compression mode: " + shared.compression.mode);
        }
        if (mode != CompressionMode::None &&
            (shared.compression.level < 1 || shared.compression.level > 19))
        {
            throw std::runtime_error("unsupported Zstd compression level");
        }
        return {
            .mode = mode,
            .level = shared.compression.level,
        };
    }

    PacketizationSettings make_packetization_settings(SharedConfig const& shared)
    {
        auto const& config = shared.packetization;
        auto settings = PacketizationSettings{
            .enabled = config.enabled,
            .max_payload_bytes = config.max_payload_bytes,
            .max_group_bytes = config.max_update_bytes,
            .max_chunks_per_group = config.max_chunks_per_update,
            .max_in_flight_groups = config.max_in_flight_updates,
            .max_incomplete_bytes = config.max_incomplete_bytes,
            .reassembly_timeout =
                Nanoseconds{static_cast<std::int64_t>(config.reassembly_timeout_ms) * 1'000'000},
        };
        validate_packetization_settings(settings);
        return settings;
    }

    SessionIdentity
    make_session_identity(SharedConfig const& shared, PipelineDefinition const& pipeline)
    {
        return {
            .application_protocol_version = application_protocol_version,
            .compatibility_fingerprint = fingerprint_network_compatibility(shared).value,
            .application_wire_fingerprint = pipeline_decode_signature(pipeline),
            .capabilities = 0,
        };
    }

    std::string_view shutdown_reason_name(ShutdownReason reason)
    {
        using enum ShutdownReason;
        switch (reason)
        {
            case None:
                return "none";
            case Requested:
                return "requested";
            case Signal:
                return "signal";
            case FrameLimit:
                return "frame_limit";
            case TickLimit:
                return "tick_limit";
            case RuntimeLimit:
                return "runtime_limit";
            case WindowClosed:
                return "window_closed";
            case TransportDisconnected:
                return "transport_disconnected";
            case FatalError:
                return "fatal_error";
        }
        return "unknown";
    }
}

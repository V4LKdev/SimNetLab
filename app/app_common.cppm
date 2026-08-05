module;

#include <charconv>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

export module simnet.app_common;

import simnet.config;
import simnet.compression;
import simnet.core;
import simnet.pipeline;
import simnet.packetization;
import simnet.runtime;
import simnet.telemetry;
import simnet.transport;

export namespace simnet::app
{
    enum class CompressionMode : std::uint8_t
    {
        None,
        WholeUpdate,
        PerPacket,
    };

    struct CompressionSettings
    {
        CompressionMode mode{CompressionMode::None};
        int level{1};
    };

    /// App-local stationary interest/debug view state.
    struct StationaryObserverState
    {
        Vec3f position{};
        float yaw{};
        float pitch{};
        float interest_radius{150.0F};
        float vertical_fov_degrees{60.0F};
    };

    [[nodiscard]] Vec3f stationary_observer_forward(StationaryObserverState const& state) noexcept;
    void apply_stationary_observer_rotation(
        StationaryObserverState& state,
        float yaw_axis,
        float pitch_axis,
        Nanoseconds frame_delta
    ) noexcept;

    class SignalHandlers
    {
    public:
        SignalHandlers();
        ~SignalHandlers();

        SignalHandlers(SignalHandlers const&) = delete;
        SignalHandlers& operator=(SignalHandlers const&) = delete;

    private:
        using Handler = void (*)(int);

        Handler interrupt_{};
        Handler terminate_{};
    };

    class TelemetryLifetime
    {
    public:
        explicit TelemetryLifetime(TelemetryConfig const& config);
        ~TelemetryLifetime();

        TelemetryLifetime(TelemetryLifetime const&) = delete;
        TelemetryLifetime& operator=(TelemetryLifetime const&) = delete;
    };

    [[nodiscard]] bool signal_stop_requested() noexcept;

    template <typename Value>
    [[nodiscard]] Value parse_unsigned(std::string_view text, std::string_view option)
    {
        auto value = Value{};
        auto const result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
            throw std::runtime_error("invalid value for " + std::string{option});
        }
        return value;
    }

    [[nodiscard]] std::string_view
    next_option_value(int& index, int argc, char** argv, std::string_view option);

    [[nodiscard]] std::chrono::milliseconds
    milliseconds_option(int& index, int argc, char** argv, std::string_view option);

    [[nodiscard]] SendSizePolicy transport_send_size_policy(TransportConfig const& config);
    [[nodiscard]] Delivery snapshot_delivery(TransportConfig const& config);
    [[nodiscard]] PipelineDefinition make_snapshot_pipeline(SharedConfig const& shared);
    [[nodiscard]] CompressionSettings make_compression_settings(SharedConfig const& shared);
    [[nodiscard]] constexpr std::string_view compression_mode_name(CompressionMode mode) noexcept;
    [[nodiscard]] PacketizationSettings make_packetization_settings(SharedConfig const& shared);
    [[nodiscard]] SessionIdentity
    make_session_identity(SharedConfig const& shared, PipelineDefinition const& pipeline);
    [[nodiscard]] std::string_view shutdown_reason_name(ShutdownReason reason);
}

namespace
{
    constexpr std::uint32_t application_protocol_version = 5;
    volatile std::sig_atomic_t signal_stop_latch = 0;

    extern "C" void request_signal_stop(int)
    {
        signal_stop_latch = 1;
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
        : interrupt_(std::signal(SIGINT, request_signal_stop))
        , terminate_(std::signal(SIGTERM, request_signal_stop))
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

    TelemetryLifetime::~TelemetryLifetime()
    {
        shutdown_telemetry();
    }

    bool signal_stop_requested() noexcept
    {
        return signal_stop_latch != 0;
    }

    std::string_view next_option_value(int& index, int argc, char** argv, std::string_view option)
    {
        if (++index >= argc) {
            throw std::runtime_error("missing value for " + std::string{option});
        }
        return argv[index];
    }

    std::chrono::milliseconds
    milliseconds_option(int& index, int argc, char** argv, std::string_view option)
    {
        auto const value
            = parse_unsigned<std::uint64_t>(next_option_value(index, argc, argv, option), option);
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw std::runtime_error("value out of range for " + std::string{option});
        }
        return std::chrono::milliseconds{static_cast<std::int64_t>(value)};
    }

    SendSizePolicy transport_send_size_policy(TransportConfig const& config)
    {
        if (config.send_size_policy == "enforce_limit") {
            return SendSizePolicy::EnforceLimit;
        }
        if (config.send_size_policy == "allow_backend_fragmentation") {
            return SendSizePolicy::AllowBackendFragmentation;
        }
        throw std::runtime_error("unsupported send size policy: " + config.send_size_policy);
    }

    Delivery snapshot_delivery(TransportConfig const& config)
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
        throw std::runtime_error("unsupported snapshot delivery: " + config.snapshot_delivery);
    }

    PipelineDefinition make_snapshot_pipeline(SharedConfig const& shared)
    {
        auto pipeline = PipelineDefinition{};
        if (shared.pipeline.enable_incremental) {
            pipeline.techniques |= PipelineTechniqueFlags::Incremental;
        }
        if (shared.pipeline.enable_quantization) {
            pipeline.techniques |= PipelineTechniqueFlags::Quantization;
            pipeline.quantization.position_bounds
                = make_centered_bounds(shared.simulation.world_half);
        }
        if (shared.pipeline.enable_delta) {
            pipeline.techniques |= PipelineTechniqueFlags::Delta;
        }
        auto const& area_of_interest = shared.pipeline.area_of_interest;
        if (area_of_interest.mode == "none") {
            pipeline.area_of_interest.mode = AreaOfInterestMode::None;
        } else if (area_of_interest.mode == "radius") {
            pipeline.area_of_interest.mode = AreaOfInterestMode::Radius;
        } else if (area_of_interest.mode == "fov") {
            pipeline.area_of_interest.mode = AreaOfInterestMode::Fov;
        } else {
            throw std::runtime_error("unsupported AOI mode: " + area_of_interest.mode);
        }
        pipeline.area_of_interest.radius = area_of_interest.radius;
        pipeline.area_of_interest.fov_degrees = area_of_interest.fov_degrees;
        validate_pipeline_definition(pipeline);
        return pipeline;
    }

    CompressionSettings make_compression_settings(SharedConfig const& shared)
    {
        auto mode = CompressionMode::None;
        if (shared.compression.mode == "whole_update") {
            mode = CompressionMode::WholeUpdate;
        } else if (shared.compression.mode == "per_packet") {
            mode = CompressionMode::PerPacket;
        } else if (shared.compression.mode != "none") {
            throw std::runtime_error("unsupported compression mode: " + shared.compression.mode);
        }
        if (mode != CompressionMode::None
            && (shared.compression.level < 1 || shared.compression.level > 19)) {
            throw std::runtime_error("unsupported Zstd compression level");
        }
        return {
            .mode = mode,
            .level = shared.compression.level,
        };
    }

    constexpr std::string_view compression_mode_name(CompressionMode mode) noexcept
    {
        switch (mode) {
            case CompressionMode::None:
                return "none";
            case CompressionMode::WholeUpdate:
                return "whole_update";
            case CompressionMode::PerPacket:
                return "per_packet";
        }
        return "unknown";
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
            .reassembly_timeout
            = Nanoseconds{static_cast<std::int64_t>(config.reassembly_timeout_ms) * 1'000'000},
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
        switch (reason) {
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

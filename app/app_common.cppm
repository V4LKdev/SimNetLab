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
import simnet.core;
import simnet.pipeline;
import simnet.runtime;
import simnet.telemetry;
import simnet.transport;

export namespace simnet::app
{
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
    [[nodiscard]] PipelineDefinition
    make_snapshot_pipeline(SharedConfig const& shared, TransportConfig const& transport);
    [[nodiscard]] SessionIdentity
    make_session_identity(SharedConfig const& shared, PipelineDefinition const& pipeline);
    [[nodiscard]] std::string_view shutdown_reason_name(ShutdownReason reason);
}

namespace
{
    constexpr std::uint32_t application_protocol_version = 3;
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

    PipelineDefinition
    make_snapshot_pipeline(SharedConfig const& shared, TransportConfig const& transport)
    {
        if (shared.pipeline.enable_aoi) {
            throw std::runtime_error("area of interest pipeline selection is not supported");
        }
        if (shared.pipeline.enable_compression) {
            throw std::runtime_error("compression pipeline selection is not supported");
        }
        if (shared.pipeline.position_bits != 16U) {
            throw std::runtime_error(
                "pipeline position_bits must be 16 until variable-width encoding is implemented"
            );
        }
        if (shared.pipeline.heading_bits != 16U) {
            throw std::runtime_error(
                "pipeline heading_bits must be 16 until variable-width encoding is implemented"
            );
        }

        auto pipeline = PipelineDefinition{
            .encoded_update_size_target_bytes = transport.max_payload_bytes,
        };
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
        validate_pipeline_definition(pipeline);
        return pipeline;
    }

    SessionIdentity
    make_session_identity(SharedConfig const& shared, PipelineDefinition const& pipeline)
    {
        return {
            .application_protocol_version = application_protocol_version,
            .compatibility_fingerprint = fingerprint_network_compatibility(shared).value,
            .pipeline_decode_signature = pipeline_decode_signature(pipeline),
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

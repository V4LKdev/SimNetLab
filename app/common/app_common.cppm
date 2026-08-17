module;

#include <charconv>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

export module simnet.app_common;

import simnet.config;
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
        ~TelemetryLifetime() noexcept;

        TelemetryLifetime(TelemetryLifetime const&) = delete;
        TelemetryLifetime& operator=(TelemetryLifetime const&) = delete;

        /// Flushes and releases telemetry. Shutdown failures are reported to the caller.
        void shutdown();

      private:
        bool active_{true};
    };

    [[nodiscard]] bool signal_stop_requested() noexcept;

    template <typename Value>
    [[nodiscard]] Value parse_unsigned(std::string_view text, std::string_view option)
    {
        auto value = Value{};
        auto const result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        {
            throw std::runtime_error("invalid value for " + std::string{option});
        }
        return value;
    }

    [[nodiscard]] std::string_view
    next_option_value(int& index, int argc, char** argv, std::string_view option);

    [[nodiscard]] std::chrono::milliseconds
    milliseconds_option(int& index, int argc, char** argv, std::string_view option);

    [[nodiscard]] TransportDelivery
    snapshot_transport_delivery(SnapshotDeliveryConfig const& config);
    [[nodiscard]] constexpr std::string_view
    transport_delivery_name(TransportDelivery delivery) noexcept
    {
        switch (delivery)
        {
            case TransportDelivery::ReliableSequenced:
                return "reliable_sequenced";
            case TransportDelivery::UnreliableSequenced:
                return "unreliable_sequenced";
        }
        return "unknown";
    }
    [[nodiscard]] PipelineDefinition make_snapshot_pipeline(SharedConfig const& shared);
    [[nodiscard]] CompressionSettings make_compression_settings(SharedConfig const& shared);
    [[nodiscard]] constexpr std::string_view compression_mode_name(CompressionMode mode) noexcept
    {
        switch (mode)
        {
            case CompressionMode::None:
                return "none";
            case CompressionMode::WholeUpdate:
                return "whole_update";
            case CompressionMode::PerPacket:
                return "per_packet";
        }
        return "unknown";
    }
    [[nodiscard]] PacketizationSettings make_packetization_settings(SharedConfig const& shared);
    [[nodiscard]] SessionIdentity
    make_session_identity(SharedConfig const& shared, PipelineDefinition const& pipeline);
    [[nodiscard]] std::string_view shutdown_reason_name(ShutdownReason reason);
}

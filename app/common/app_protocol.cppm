module;

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// @brief Application-owned reliable control and latest-state input messages.
export module simnet.app_protocol;

import simnet.core;
import simnet.transport;

export namespace simnet::app
{
    inline constexpr std::uint32_t application_protocol_version = 7U;
    inline constexpr std::uint8_t app_message_version = 2U;
    inline constexpr Nanoseconds stationary_observer_interest_min_interval{50'000'000};
    inline constexpr Nanoseconds stationary_observer_interest_heartbeat_interval{500'000'000};
    inline constexpr TransportLane control_lane{TransportLane::Lane0};
    inline constexpr TransportLane snapshot_lane{TransportLane::Lane1};
    inline constexpr TransportLane input_lane{TransportLane::Lane2};

    enum class ClientRole : std::uint8_t
    {
        StationaryObserver = 0,
        Player = 1
    };

    enum class AppMessageKind : std::uint8_t
    {
        Invalid = 0,
        PauseSetRequest = 1,
        PauseState = 2,
        JoinRequest = 3,
        JoinAccepted = 4,
        SnapshotAck = 5,
        PlayerInput = 6,
        StationaryObserverInterest = 7,
        SnapshotRecoveryRequest = 8
    };

    struct AppMessage
    {
        AppMessageKind kind{AppMessageKind::Invalid};
        ClientRole role{ClientRole::StationaryObserver};
        PeerId peer_id{};
        EntityNetId player_id{};
        bool paused{};
    };

    enum class PlayerButton : std::uint8_t
    {
        W = 1U << 0U,
        A = 1U << 1U,
        S = 1U << 2U,
        D = 1U << 3U,
        Shift = 1U << 4U,
        Control = 1U << 5U,
        LeftMouse = 1U << 6U,
        RightMouse = 1U << 7U
    };

    struct PlayerInputMessage
    {
        std::uint8_t buttons{};
    };

    /// Cumulative acknowledgment of decoded and applied snapshot groups.
    struct SnapshotAck
    {
        SequenceId newest_received_snapshot{};
        std::uint32_t received_mask{};
        SequenceId newest_applied_snapshot{};
    };

    /// Requests a self-contained update after an exact Patch baseline is unavailable.
    struct SnapshotRecoveryRequest
    {
        SequenceId rejected_update_sequence{};
        SequenceId missing_baseline_sequence{};
    };

    /// Versioned stationary observer pose sent on the application input lane.
    struct StationaryObserverInterestMessage
    {
        Vec3f position{};
        Vec3f forward{.z = 1.0F};
    };

    /// Per-session accepted stationary observer state.
    struct StationaryObserverInterestState
    {
        bool initialized{};
        Vec3f position{};
        Vec3f forward{.z = 1.0F};
        Nanoseconds last_accepted_time{};
    };

    enum class StationaryObserverInterestResult : std::uint8_t
    {
        Accepted,
        RateLimited,
        PositionChanged,
        TimeWentBackward,
    };

    [[nodiscard]] constexpr bool button_down(PlayerInputMessage input, PlayerButton button) noexcept
    {
        return (input.buttons & static_cast<std::uint8_t>(button)) != 0U;
    }

    /// Returns a recognized kind after validating the application message version.
    [[nodiscard]] std::optional<AppMessageKind>
    decode_app_message_kind(std::span<Byte const> bytes) noexcept;

    [[nodiscard]] std::vector<Byte> encode_app_message(AppMessage message);

    /// Decodes a complete pause or join message. Failure leaves the destination unchanged.
    [[nodiscard]] bool
    decode_app_message(std::span<Byte const> bytes, AppMessage& message) noexcept;

    [[nodiscard]] std::vector<Byte> encode_player_input(PlayerInputMessage input);

    /// Decodes one complete Player input message. Failure leaves the destination unchanged.
    [[nodiscard]] bool
    decode_player_input(std::span<Byte const> bytes, PlayerInputMessage& input) noexcept;

    /// Encodes a snapshot acknowledgment as a 14-byte control message.
    [[nodiscard]] std::vector<Byte> encode_snapshot_ack(SnapshotAck const& ack);

    /// Decodes one complete snapshot acknowledgment. Failure leaves the destination unchanged.
    [[nodiscard]] bool decode_snapshot_ack(std::span<Byte const> bytes, SnapshotAck& ack) noexcept;

    /// Encodes a snapshot recovery request as a 10-byte control message.
    [[nodiscard]] std::vector<Byte>
    encode_snapshot_recovery_request(SnapshotRecoveryRequest const& request);

    /// Decodes one valid recovery request. Failure leaves the destination unchanged.
    [[nodiscard]] bool decode_snapshot_recovery_request(
        std::span<Byte const> bytes,
        SnapshotRecoveryRequest& request
    ) noexcept;

    /// Encodes stationary observer interest as a 26-byte application message.
    [[nodiscard]] std::vector<Byte>
    encode_stationary_observer_interest(StationaryObserverInterestMessage const& message);

    /// Decodes finite observer interest and normalizes its direction transactionally.
    [[nodiscard]] bool decode_stationary_observer_interest(
        std::span<Byte const> bytes,
        StationaryObserverInterestMessage& message
    ) noexcept;

    /// Applies a valid stationary observer update without changing locked session position.
    [[nodiscard]] StationaryObserverInterestResult accept_stationary_observer_interest(
        StationaryObserverInterestState& state,
        StationaryObserverInterestMessage const& message,
        Nanoseconds now
    ) noexcept;
}

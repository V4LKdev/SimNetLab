module;

#include <chrono>
#include <cmath>
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
        AppMessageKind kind{};
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

    /// Cumulative acknowledgement of decoded and applied snapshot groups.
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

    [[nodiscard]] inline bool valid_role(ClientRole role) noexcept
    {
        return role == ClientRole::StationaryObserver || role == ClientRole::Player;
    }

    [[nodiscard]] inline std::optional<AppMessageKind>
    decode_app_message_kind(std::span<Byte const> bytes) noexcept
    {
        if (bytes.size() < 2U || bytes[1] != static_cast<Byte>(app_message_version))
        {
            return std::nullopt;
        }
        auto const kind = static_cast<AppMessageKind>(bytes[0]);
        switch (kind)
        {
            case AppMessageKind::PauseSetRequest:
            case AppMessageKind::PauseState:
            case AppMessageKind::JoinRequest:
            case AppMessageKind::JoinAccepted:
            case AppMessageKind::SnapshotAck:
            case AppMessageKind::PlayerInput:
            case AppMessageKind::StationaryObserverInterest:
            case AppMessageKind::SnapshotRecoveryRequest:
                return kind;
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::vector<Byte> encode_app_message(AppMessage message)
    {
        auto bytes = std::vector<Byte>{
            static_cast<Byte>(message.kind),
            static_cast<Byte>(app_message_version),
        };
        switch (message.kind)
        {
            case AppMessageKind::PauseSetRequest:
            case AppMessageKind::PauseState:
                bytes.push_back(static_cast<Byte>(message.paused ? 1U : 0U));
                break;
            case AppMessageKind::JoinRequest:
                bytes.push_back(static_cast<Byte>(message.role));
                break;
            case AppMessageKind::JoinAccepted:
                bytes.push_back(static_cast<Byte>(message.role));
                append_big_endian(bytes, message.peer_id);
                append_big_endian(bytes, message.player_id);
                break;
            case AppMessageKind::SnapshotAck:
            case AppMessageKind::PlayerInput:
            case AppMessageKind::StationaryObserverInterest:
            case AppMessageKind::SnapshotRecoveryRequest:
                return {};
        }
        return bytes;
    }

    [[nodiscard]] inline bool
    decode_app_message(std::span<Byte const> bytes, AppMessage& message) noexcept
    {
        if (bytes.size() < 2U || bytes[1] != static_cast<Byte>(app_message_version))
        {
            return false;
        }
        auto decoded = AppMessage{
            .kind = static_cast<AppMessageKind>(bytes[0]),
        };
        switch (decoded.kind)
        {
            case AppMessageKind::PauseSetRequest:
            case AppMessageKind::PauseState:
                if (bytes.size() != 3U || (bytes[2] != Byte{0U} && bytes[2] != Byte{1U}))
                {
                    return false;
                }
                decoded.paused = bytes[2] == Byte{1U};
                break;
            case AppMessageKind::JoinRequest:
                if (bytes.size() != 3U)
                {
                    return false;
                }
                decoded.role = static_cast<ClientRole>(bytes[2]);
                if (!valid_role(decoded.role))
                {
                    return false;
                }
                break;
            case AppMessageKind::JoinAccepted:
            {
                auto peer_id = std::uint16_t{};
                auto player_id = std::uint32_t{};
                auto offset = std::size_t{3U};
                if (bytes.size() != 9U || !read_big_endian(bytes, offset, peer_id) ||
                    !read_big_endian(bytes, offset, player_id))
                {
                    return false;
                }
                decoded.role = static_cast<ClientRole>(bytes[2]);
                if (!valid_role(decoded.role) || peer_id == 0U ||
                    (decoded.role == ClientRole::StationaryObserver && player_id != 0U) ||
                    (decoded.role == ClientRole::Player && player_id == 0U))
                {
                    return false;
                }
                decoded.peer_id = peer_id;
                decoded.player_id = player_id;
                break;
            }
            default:
                return false;
        }
        message = decoded;
        return true;
    }

    [[nodiscard]] inline std::vector<Byte> encode_player_input(PlayerInputMessage input)
    {
        return {
            static_cast<Byte>(AppMessageKind::PlayerInput),
            static_cast<Byte>(app_message_version),
            static_cast<Byte>(input.buttons),
        };
    }

    [[nodiscard]] inline bool
    decode_player_input(std::span<Byte const> bytes, PlayerInputMessage& input) noexcept
    {
        if (bytes.size() != 3U || bytes[0] != static_cast<Byte>(AppMessageKind::PlayerInput) ||
            bytes[1] != static_cast<Byte>(app_message_version))
        {
            return false;
        }
        input = {.buttons = static_cast<std::uint8_t>(bytes[2])};
        return true;
    }

    [[nodiscard]] inline std::vector<Byte> encode_snapshot_ack(SnapshotAck const& ack)
    {
        auto bytes = std::vector<Byte>{
            static_cast<Byte>(AppMessageKind::SnapshotAck),
            static_cast<Byte>(app_message_version),
        };
        bytes.reserve(14U);
        append_big_endian(bytes, ack.newest_received_snapshot);
        append_big_endian(bytes, ack.received_mask);
        append_big_endian(bytes, ack.newest_applied_snapshot);
        return bytes;
    }

    [[nodiscard]] inline bool
    decode_snapshot_ack(std::span<Byte const> bytes, SnapshotAck& ack) noexcept
    {
        auto decoded = SnapshotAck{};
        auto offset = std::size_t{2U};
        if (bytes.size() != 14U || bytes[0] != static_cast<Byte>(AppMessageKind::SnapshotAck) ||
            bytes[1] != static_cast<Byte>(app_message_version) ||
            !read_big_endian(bytes, offset, decoded.newest_received_snapshot) ||
            !read_big_endian(bytes, offset, decoded.received_mask) ||
            !read_big_endian(bytes, offset, decoded.newest_applied_snapshot))
        {
            return false;
        }
        ack = decoded;
        return true;
    }

    [[nodiscard]] inline std::vector<Byte>
    encode_snapshot_recovery_request(SnapshotRecoveryRequest const& request)
    {
        auto bytes = std::vector<Byte>{
            static_cast<Byte>(AppMessageKind::SnapshotRecoveryRequest),
            static_cast<Byte>(app_message_version),
        };
        bytes.reserve(10U);
        append_big_endian(bytes, request.rejected_update_sequence);
        append_big_endian(bytes, request.missing_baseline_sequence);
        return bytes;
    }

    [[nodiscard]] inline bool decode_snapshot_recovery_request(
        std::span<Byte const> bytes,
        SnapshotRecoveryRequest& request
    ) noexcept
    {
        auto decoded = SnapshotRecoveryRequest{};
        auto offset = std::size_t{2U};
        if (bytes.size() != 10U ||
            bytes[0] != static_cast<Byte>(AppMessageKind::SnapshotRecoveryRequest) ||
            bytes[1] != static_cast<Byte>(app_message_version) ||
            !read_big_endian(bytes, offset, decoded.rejected_update_sequence) ||
            !read_big_endian(bytes, offset, decoded.missing_baseline_sequence) ||
            decoded.rejected_update_sequence == 0U || decoded.missing_baseline_sequence == 0U ||
            decoded.missing_baseline_sequence >= decoded.rejected_update_sequence)
        {
            return false;
        }
        request = decoded;
        return true;
    }

    [[nodiscard]] inline std::vector<Byte>
    encode_stationary_observer_interest(StationaryObserverInterestMessage const& message)
    {
        auto bytes = std::vector<Byte>{
            static_cast<Byte>(AppMessageKind::StationaryObserverInterest),
            static_cast<Byte>(app_message_version),
        };
        bytes.reserve(26U);
        append_float32_big_endian(bytes, message.position.x);
        append_float32_big_endian(bytes, message.position.y);
        append_float32_big_endian(bytes, message.position.z);
        append_float32_big_endian(bytes, message.forward.x);
        append_float32_big_endian(bytes, message.forward.y);
        append_float32_big_endian(bytes, message.forward.z);
        return bytes;
    }

    [[nodiscard]] inline bool decode_stationary_observer_interest(
        std::span<Byte const> bytes,
        StationaryObserverInterestMessage& message
    ) noexcept
    {
        if (bytes.size() != 26U ||
            bytes[0] != static_cast<Byte>(AppMessageKind::StationaryObserverInterest) ||
            bytes[1] != static_cast<Byte>(app_message_version))
        {
            return false;
        }

        auto decoded = StationaryObserverInterestMessage{};
        auto offset = std::size_t{2U};
        if (!read_float32_big_endian(bytes, offset, decoded.position.x) ||
            !read_float32_big_endian(bytes, offset, decoded.position.y) ||
            !read_float32_big_endian(bytes, offset, decoded.position.z) ||
            !read_float32_big_endian(bytes, offset, decoded.forward.x) ||
            !read_float32_big_endian(bytes, offset, decoded.forward.y) ||
            !read_float32_big_endian(bytes, offset, decoded.forward.z) ||
            !is_finite(decoded.position) || !is_finite(decoded.forward))
        {
            return false;
        }
        auto const forward_length_squared = length_squared(decoded.forward);
        if (!std::isfinite(forward_length_squared) || forward_length_squared <= 0.0F)
        {
            return false;
        }
        decoded.forward = decoded.forward / std::sqrt(forward_length_squared);
        message = decoded;
        return true;
    }

    [[nodiscard]] inline StationaryObserverInterestResult accept_stationary_observer_interest(
        StationaryObserverInterestState& state,
        StationaryObserverInterestMessage const& message,
        Nanoseconds now
    ) noexcept
    {
        if (state.initialized)
        {
            if (message.position.x != state.position.x || message.position.y != state.position.y ||
                message.position.z != state.position.z)
            {
                return StationaryObserverInterestResult::PositionChanged;
            }
            if (now < state.last_accepted_time)
            {
                return StationaryObserverInterestResult::TimeWentBackward;
            }
            if (now - state.last_accepted_time < stationary_observer_interest_min_interval)
            {
                return StationaryObserverInterestResult::RateLimited;
            }
        }

        state.initialized = true;
        state.position = message.position;
        state.forward = message.forward;
        state.last_accepted_time = now;
        return StationaryObserverInterestResult::Accepted;
    }
}

module;

#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

module simnet.app_protocol;

import simnet.core;

namespace
{
    constexpr std::size_t application_message_header_bytes = 2U;
    constexpr std::size_t pause_message_bytes = 3U;
    constexpr std::size_t join_request_bytes = 3U;
    constexpr std::size_t join_accepted_bytes = 9U;
    constexpr std::size_t player_input_bytes = 3U;
    constexpr std::size_t snapshot_ack_bytes = 14U;
    constexpr std::size_t snapshot_recovery_request_bytes = 10U;
    constexpr std::size_t stationary_observer_interest_bytes = 26U;

    [[nodiscard]] bool valid_role(simnet::app::ClientRole role) noexcept
    {
        return role == simnet::app::ClientRole::StationaryObserver ||
               role == simnet::app::ClientRole::Player;
    }

    [[nodiscard]] bool decode_pause_message(
        std::span<simnet::Byte const> bytes,
        simnet::app::AppMessage& decoded
    ) noexcept
    {
        if (bytes.size() != pause_message_bytes ||
            (bytes[2] != simnet::Byte{0U} && bytes[2] != simnet::Byte{1U}))
        {
            return false;
        }
        decoded.paused = bytes[2] == simnet::Byte{1U};
        return true;
    }

    [[nodiscard]] bool decode_join_request(
        std::span<simnet::Byte const> bytes,
        simnet::app::AppMessage& decoded
    ) noexcept
    {
        if (bytes.size() != join_request_bytes)
        {
            return false;
        }
        decoded.role = static_cast<simnet::app::ClientRole>(bytes[2]);
        return valid_role(decoded.role);
    }

    [[nodiscard]] bool decode_join_accepted(
        std::span<simnet::Byte const> bytes,
        simnet::app::AppMessage& decoded
    ) noexcept
    {
        auto peer_id = std::uint16_t{};
        auto player_id = std::uint32_t{};
        auto offset = std::size_t{3U};
        if (bytes.size() != join_accepted_bytes ||
            !simnet::read_big_endian(bytes, offset, peer_id) ||
            !simnet::read_big_endian(bytes, offset, player_id))
        {
            return false;
        }

        decoded.role = static_cast<simnet::app::ClientRole>(bytes[2]);
        if (!valid_role(decoded.role) || peer_id == 0U ||
            (decoded.role == simnet::app::ClientRole::StationaryObserver && player_id != 0U) ||
            (decoded.role == simnet::app::ClientRole::Player && player_id == 0U))
        {
            return false;
        }
        decoded.peer_id = peer_id;
        decoded.player_id = player_id;
        return true;
    }
}

namespace simnet::app
{
    std::optional<AppMessageKind> decode_app_message_kind(std::span<Byte const> bytes) noexcept
    {
        if (bytes.size() < application_message_header_bytes ||
            bytes[1] != static_cast<Byte>(app_message_version))
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
            case AppMessageKind::Invalid:
                return std::nullopt;
        }
        return std::nullopt;
    }

    std::vector<Byte> encode_app_message(AppMessage message)
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
            case AppMessageKind::Invalid:
            case AppMessageKind::SnapshotAck:
            case AppMessageKind::PlayerInput:
            case AppMessageKind::StationaryObserverInterest:
            case AppMessageKind::SnapshotRecoveryRequest:
                return {};
        }
        return bytes;
    }

    bool decode_app_message(std::span<Byte const> bytes, AppMessage& message) noexcept
    {
        if (bytes.size() < application_message_header_bytes ||
            bytes[1] != static_cast<Byte>(app_message_version))
        {
            return false;
        }

        auto decoded = AppMessage{
            .kind = static_cast<AppMessageKind>(bytes[0]),
        };
        auto valid = false;
        switch (decoded.kind)
        {
            case AppMessageKind::PauseSetRequest:
            case AppMessageKind::PauseState:
                valid = decode_pause_message(bytes, decoded);
                break;
            case AppMessageKind::JoinRequest:
                valid = decode_join_request(bytes, decoded);
                break;
            case AppMessageKind::JoinAccepted:
                valid = decode_join_accepted(bytes, decoded);
                break;
            case AppMessageKind::Invalid:
            case AppMessageKind::SnapshotAck:
            case AppMessageKind::PlayerInput:
            case AppMessageKind::StationaryObserverInterest:
            case AppMessageKind::SnapshotRecoveryRequest:
                return false;
        }
        if (!valid)
        {
            return false;
        }

        message = decoded;
        return true;
    }

    std::vector<Byte> encode_player_input(PlayerInputMessage input)
    {
        return {
            static_cast<Byte>(AppMessageKind::PlayerInput),
            static_cast<Byte>(app_message_version),
            static_cast<Byte>(input.buttons),
        };
    }

    bool decode_player_input(std::span<Byte const> bytes, PlayerInputMessage& input) noexcept
    {
        if (bytes.size() != player_input_bytes ||
            bytes[0] != static_cast<Byte>(AppMessageKind::PlayerInput) ||
            bytes[1] != static_cast<Byte>(app_message_version))
        {
            return false;
        }
        input = {.buttons = static_cast<std::uint8_t>(bytes[2])};
        return true;
    }

    std::vector<Byte> encode_snapshot_ack(SnapshotAck const& ack)
    {
        auto bytes = std::vector<Byte>{
            static_cast<Byte>(AppMessageKind::SnapshotAck),
            static_cast<Byte>(app_message_version),
        };
        bytes.reserve(snapshot_ack_bytes);
        append_big_endian(bytes, ack.newest_received_snapshot);
        append_big_endian(bytes, ack.received_mask);
        append_big_endian(bytes, ack.newest_applied_snapshot);
        return bytes;
    }

    bool decode_snapshot_ack(std::span<Byte const> bytes, SnapshotAck& ack) noexcept
    {
        auto decoded = SnapshotAck{};
        auto offset = application_message_header_bytes;
        if (bytes.size() != snapshot_ack_bytes ||
            bytes[0] != static_cast<Byte>(AppMessageKind::SnapshotAck) ||
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

    std::vector<Byte> encode_snapshot_recovery_request(SnapshotRecoveryRequest const& request)
    {
        auto bytes = std::vector<Byte>{
            static_cast<Byte>(AppMessageKind::SnapshotRecoveryRequest),
            static_cast<Byte>(app_message_version),
        };
        bytes.reserve(snapshot_recovery_request_bytes);
        append_big_endian(bytes, request.rejected_update_sequence);
        append_big_endian(bytes, request.missing_baseline_sequence);
        return bytes;
    }

    bool decode_snapshot_recovery_request(
        std::span<Byte const> bytes,
        SnapshotRecoveryRequest& request
    ) noexcept
    {
        auto decoded = SnapshotRecoveryRequest{};
        auto offset = application_message_header_bytes;
        if (bytes.size() != snapshot_recovery_request_bytes ||
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

    std::vector<Byte>
    encode_stationary_observer_interest(StationaryObserverInterestMessage const& message)
    {
        auto bytes = std::vector<Byte>{
            static_cast<Byte>(AppMessageKind::StationaryObserverInterest),
            static_cast<Byte>(app_message_version),
        };
        bytes.reserve(stationary_observer_interest_bytes);
        append_float32_big_endian(bytes, message.position.x);
        append_float32_big_endian(bytes, message.position.y);
        append_float32_big_endian(bytes, message.position.z);
        append_float32_big_endian(bytes, message.forward.x);
        append_float32_big_endian(bytes, message.forward.y);
        append_float32_big_endian(bytes, message.forward.z);
        return bytes;
    }

    bool decode_stationary_observer_interest(
        std::span<Byte const> bytes,
        StationaryObserverInterestMessage& message
    ) noexcept
    {
        if (bytes.size() != stationary_observer_interest_bytes ||
            bytes[0] != static_cast<Byte>(AppMessageKind::StationaryObserverInterest) ||
            bytes[1] != static_cast<Byte>(app_message_version))
        {
            return false;
        }

        auto decoded = StationaryObserverInterestMessage{};
        auto offset = application_message_header_bytes;
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

    StationaryObserverInterestResult accept_stationary_observer_interest(
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

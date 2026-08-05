module;

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

/// @brief Application-owned reliable control and latest-state input messages.
export module simnet.app_protocol;

import simnet.core;

export namespace simnet::app
{
    inline constexpr std::uint8_t app_message_version = 1U;
    inline constexpr std::uint8_t player_input_version = 1U;
    inline constexpr std::uint8_t stationary_observer_interest_version = 1U;
    inline constexpr Nanoseconds stationary_observer_interest_min_interval{50'000'000};
    inline constexpr Nanoseconds stationary_observer_interest_heartbeat_interval{500'000'000};

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
        JoinAccepted = 4
    };

    struct AppMessage
    {
        AppMessageKind kind{};
        ClientRole role{ClientRole::StationaryObserver};
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

    inline void write_u32(std::vector<Byte>& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<Byte>((value >> 24U) & 0xFFU));
        bytes.push_back(static_cast<Byte>((value >> 16U) & 0xFFU));
        bytes.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
        bytes.push_back(static_cast<Byte>(value & 0xFFU));
    }

    inline void write_f32(std::vector<Byte>& bytes, float value)
    {
        static_assert(
            std::numeric_limits<float>::is_iec559 && sizeof(float) == sizeof(std::uint32_t)
        );
        write_u32(bytes, std::bit_cast<std::uint32_t>(value));
    }

    [[nodiscard]] inline bool
    read_u32(std::span<Byte const> bytes, std::size_t offset, std::uint32_t& value) noexcept
    {
        if (offset + 4U > bytes.size()) {
            return false;
        }
        value = (static_cast<std::uint32_t>(bytes[offset]) << 24U)
            | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U)
            | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U)
            | static_cast<std::uint32_t>(bytes[offset + 3U]);
        return true;
    }

    [[nodiscard]] inline bool
    read_f32(std::span<Byte const> bytes, std::size_t offset, float& value) noexcept
    {
        auto bits = std::uint32_t{};
        if (!read_u32(bytes, offset, bits)) {
            return false;
        }
        value = std::bit_cast<float>(bits);
        return true;
    }

    [[nodiscard]] inline std::vector<Byte> encode_app_message(AppMessage message)
    {
        auto bytes = std::vector<Byte>{
            static_cast<Byte>(message.kind),
            static_cast<Byte>(app_message_version),
        };
        switch (message.kind) {
            case AppMessageKind::PauseSetRequest:
            case AppMessageKind::PauseState:
                bytes.push_back(static_cast<Byte>(message.paused ? 1U : 0U));
                break;
            case AppMessageKind::JoinRequest:
                bytes.push_back(static_cast<Byte>(message.role));
                break;
            case AppMessageKind::JoinAccepted:
                bytes.push_back(static_cast<Byte>(message.role));
                write_u32(bytes, message.player_id);
                break;
        }
        return bytes;
    }

    [[nodiscard]] inline bool
    decode_app_message(std::span<Byte const> bytes, AppMessage& message) noexcept
    {
        if (bytes.size() < 2U || bytes[1] != static_cast<Byte>(app_message_version)) {
            return false;
        }
        auto decoded = AppMessage{
            .kind = static_cast<AppMessageKind>(bytes[0]),
        };
        switch (decoded.kind) {
            case AppMessageKind::PauseSetRequest:
            case AppMessageKind::PauseState:
                if (bytes.size() != 3U || (bytes[2] != Byte{0U} && bytes[2] != Byte{1U})) {
                    return false;
                }
                decoded.paused = bytes[2] == Byte{1U};
                break;
            case AppMessageKind::JoinRequest:
                if (bytes.size() != 3U) {
                    return false;
                }
                decoded.role = static_cast<ClientRole>(bytes[2]);
                if (!valid_role(decoded.role)) {
                    return false;
                }
                break;
            case AppMessageKind::JoinAccepted: {
                auto player_id = std::uint32_t{};
                if (bytes.size() != 7U || !read_u32(bytes, 3U, player_id)) {
                    return false;
                }
                decoded.role = static_cast<ClientRole>(bytes[2]);
                if (!valid_role(decoded.role)
                    || (decoded.role == ClientRole::StationaryObserver && player_id != 0U)
                    || (decoded.role == ClientRole::Player && player_id == 0U)) {
                    return false;
                }
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
            static_cast<Byte>(player_input_version),
            static_cast<Byte>(input.buttons),
        };
    }

    [[nodiscard]] inline bool
    decode_player_input(std::span<Byte const> bytes, PlayerInputMessage& input) noexcept
    {
        if (bytes.size() != 2U || bytes[0] != static_cast<Byte>(player_input_version)) {
            return false;
        }
        input = {.buttons = static_cast<std::uint8_t>(bytes[1])};
        return true;
    }

    [[nodiscard]] inline std::vector<Byte>
    encode_stationary_observer_interest(StationaryObserverInterestMessage const& message)
    {
        auto bytes = std::vector<Byte>{
            static_cast<Byte>(stationary_observer_interest_version),
        };
        bytes.reserve(25U);
        write_f32(bytes, message.position.x);
        write_f32(bytes, message.position.y);
        write_f32(bytes, message.position.z);
        write_f32(bytes, message.forward.x);
        write_f32(bytes, message.forward.y);
        write_f32(bytes, message.forward.z);
        return bytes;
    }

    [[nodiscard]] inline bool decode_stationary_observer_interest(
        std::span<Byte const> bytes,
        StationaryObserverInterestMessage& message
    ) noexcept
    {
        if (bytes.size() != 25U
            || bytes[0] != static_cast<Byte>(stationary_observer_interest_version)) {
            return false;
        }

        auto decoded = StationaryObserverInterestMessage{};
        if (!read_f32(bytes, 1U, decoded.position.x) || !read_f32(bytes, 5U, decoded.position.y)
            || !read_f32(bytes, 9U, decoded.position.z) || !read_f32(bytes, 13U, decoded.forward.x)
            || !read_f32(bytes, 17U, decoded.forward.y) || !read_f32(bytes, 21U, decoded.forward.z)
            || !is_finite(decoded.position) || !is_finite(decoded.forward)) {
            return false;
        }
        auto const forward_length_squared = length_squared(decoded.forward);
        if (!std::isfinite(forward_length_squared) || forward_length_squared <= 0.0F) {
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
        if (state.initialized) {
            if (message.position.x != state.position.x || message.position.y != state.position.y
                || message.position.z != state.position.z) {
                return StationaryObserverInterestResult::PositionChanged;
            }
            if (now < state.last_accepted_time) {
                return StationaryObserverInterestResult::TimeWentBackward;
            }
            if (now - state.last_accepted_time < stationary_observer_interest_min_interval) {
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

module;

#include <cstdint>
#include <span>
#include <vector>

/// @brief Application-owned reliable control and latest-state input messages.
export module simnet.app_protocol;

import simnet.core;

export namespace simnet::app
{
    inline constexpr std::uint8_t app_message_version = 1U;
    inline constexpr std::uint8_t player_input_version = 1U;

    enum class ClientRole : std::uint8_t
    {
        Observer,
        Player
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
        AppMessageKind kind {};
        ClientRole role { ClientRole::Observer };
        EntityNetId player_id {};
        bool paused {};
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
        std::uint8_t buttons {};
    };

    [[nodiscard]] constexpr bool button_down(
        PlayerInputMessage input,
        PlayerButton button
    ) noexcept
    {
        return (input.buttons & static_cast<std::uint8_t>(button)) != 0U;
    }

    [[nodiscard]] inline bool valid_role(ClientRole role) noexcept
    {
        return role == ClientRole::Observer || role == ClientRole::Player;
    }

    inline void write_u32(std::vector<Byte>& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<Byte>((value >> 24U) & 0xFFU));
        bytes.push_back(static_cast<Byte>((value >> 16U) & 0xFFU));
        bytes.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
        bytes.push_back(static_cast<Byte>(value & 0xFFU));
    }

    [[nodiscard]] inline bool read_u32(
        std::span<Byte const> bytes,
        std::size_t offset,
        std::uint32_t& value
    ) noexcept
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

    [[nodiscard]] inline std::vector<Byte> encode_app_message(AppMessage message)
    {
        auto bytes = std::vector<Byte> {
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

    [[nodiscard]] inline bool decode_app_message(
        std::span<Byte const> bytes,
        AppMessage& message
    ) noexcept
    {
        if (bytes.size() < 2U || bytes[1] != static_cast<Byte>(app_message_version)) {
            return false;
        }
        auto decoded = AppMessage {
            .kind = static_cast<AppMessageKind>(bytes[0]),
        };
        switch (decoded.kind) {
        case AppMessageKind::PauseSetRequest:
        case AppMessageKind::PauseState:
            if (bytes.size() != 3U || (bytes[2] != Byte { 0U } && bytes[2] != Byte { 1U })) {
                return false;
            }
            decoded.paused = bytes[2] == Byte { 1U };
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
            auto player_id = std::uint32_t {};
            if (bytes.size() != 7U
                || !read_u32(bytes, 3U, player_id)) {
                return false;
            }
            decoded.role = static_cast<ClientRole>(bytes[2]);
            if (!valid_role(decoded.role)
                || (decoded.role == ClientRole::Observer && player_id != 0U)
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

    [[nodiscard]] inline bool decode_player_input(
        std::span<Byte const> bytes,
        PlayerInputMessage& input
    ) noexcept
    {
        if (bytes.size() != 2U || bytes[0] != static_cast<Byte>(player_input_version)) {
            return false;
        }
        input = { .buttons = static_cast<std::uint8_t>(bytes[1]) };
        return true;
    }
}

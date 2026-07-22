module;

#include <cstdint>
#include <span>
#include <vector>

/// @brief Application-owned messages carried by transport application control.
export module simnet.app_protocol;

import simnet.core;

export namespace simnet::app
{
    enum class AppMessageKind : std::uint8_t
    {
        PauseSetRequest = 1,
        PauseState = 2
    };

    struct AppMessage
    {
        AppMessageKind kind {};
        bool paused {};
    };

    [[nodiscard]] inline std::vector<Byte> encode_app_message(AppMessage message)
    {
        return {
            static_cast<Byte>(message.kind),
            static_cast<Byte>(message.paused ? 1U : 0U),
        };
    }

    [[nodiscard]] inline bool decode_app_message(
        std::span<Byte const> bytes,
        AppMessage& message
    ) noexcept
    {
        if (bytes.size() != 2U || (bytes[1] != Byte { 0U } && bytes[1] != Byte { 1U })) {
            return false;
        }

        auto const kind = static_cast<AppMessageKind>(bytes[0]);
        if (kind != AppMessageKind::PauseSetRequest && kind != AppMessageKind::PauseState) {
            return false;
        }

        message = {
            .kind = kind,
            .paused = bytes[1] == Byte { 1U },
        };
        return true;
    }
}

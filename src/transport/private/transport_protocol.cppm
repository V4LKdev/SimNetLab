module;

#include <cstddef>
#include <cstdint>
#include <vector>

module simnet.transport:protocol;

import :types;
import simnet.core;

namespace simnet::transport_protocol
{
    inline constexpr std::uint8_t channel_count = 3;
    inline constexpr std::size_t max_session_message_bytes = 31;
    inline constexpr std::size_t max_reassembled_payload_bytes = std::size_t{16} * 1024U * 1024U;

    enum class SessionMessageKind : std::uint8_t
    {
        Invalid = 0,
        ClientHello = 1,
        ServerAccept = 2,
        ServerReject = 3
    };

    struct SessionMessage
    {
        SessionMessageKind kind{};
        SessionIdentity identity{};
        DisconnectCode reject_code{};
    };

    [[nodiscard]] DisconnectCode
    identity_mismatch(SessionIdentity const& actual, SessionIdentity const& expected) noexcept;

    [[nodiscard]] std::uint8_t lane_index(TransportLane lane) noexcept;
    [[nodiscard]] bool valid_lane(TransportLane lane) noexcept;
    [[nodiscard]] bool valid_delivery(TransportDelivery delivery) noexcept;
    [[nodiscard]] bool valid_disconnect_code(DisconnectCode code) noexcept;

    [[nodiscard]] std::vector<Byte> encode_session_message(SessionMessage const& message);
    [[nodiscard]] bool decode_session_message(ByteSpan bytes, SessionMessage& message);
} // namespace simnet::transport_protocol

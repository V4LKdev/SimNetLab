module;

#include <cstddef>
#include <cstdint>
#include <vector>

module simnet.transport:protocol;

import :types;

namespace simnet::transport_protocol
{
    inline constexpr std::uint8_t channel_count = 3;
    inline constexpr std::size_t max_session_message_bytes = 35;
    inline constexpr std::size_t max_application_control_bytes = 256;
    inline constexpr std::size_t max_control_message_bytes = 11 + max_application_control_bytes;
    inline constexpr std::size_t max_application_input_bytes = 32;
    inline constexpr std::size_t max_input_message_bytes = 11 + max_application_input_bytes;
    inline constexpr std::size_t max_reassembled_payload_bytes = 16U * 1024U * 1024U;

    enum class SessionMessageKind : std::uint8_t
    {
        ClientHello = 1,
        ServerAccept = 2,
        ServerReject = 3,
        SnapshotAck = 4,
        ApplicationControl = 5,
        ApplicationInput = 6
    };

    struct SessionMessage
    {
        SessionMessageKind kind{};
        SessionIdentity identity{};
        DisconnectCode reject_code{};
        SnapshotAck snapshot_ack{};
        std::vector<Byte> application_control{};
        std::vector<Byte> application_input{};
    };

    [[nodiscard]] DisconnectCode
    identity_mismatch(SessionIdentity const& actual, SessionIdentity const& expected) noexcept;

    [[nodiscard]] std::uint8_t lane_index(Lane lane) noexcept;
    [[nodiscard]] bool valid_lane(Lane lane) noexcept;
    [[nodiscard]] bool valid_delivery(Delivery delivery) noexcept;
    [[nodiscard]] bool valid_disconnect_code(DisconnectCode code) noexcept;

    void write_u8(std::vector<Byte>& bytes, std::uint8_t value);
    void write_u16(std::vector<Byte>& bytes, std::uint16_t value);
    void write_u32(std::vector<Byte>& bytes, std::uint32_t value);
    void write_u64(std::vector<Byte>& bytes, std::uint64_t value);

    [[nodiscard]] bool
    read_u8(Byte const* data, std::size_t size, std::size_t& offset, std::uint8_t& value);
    [[nodiscard]] bool
    read_u16(Byte const* data, std::size_t size, std::size_t& offset, std::uint16_t& value);
    [[nodiscard]] bool
    read_u32(Byte const* data, std::size_t size, std::size_t& offset, std::uint32_t& value);
    [[nodiscard]] bool
    read_u64(Byte const* data, std::size_t size, std::size_t& offset, std::uint64_t& value);

    [[nodiscard]] std::vector<Byte> encode_session_message(SessionMessage const& message);
    [[nodiscard]] bool
    decode_session_message(Byte const* data, std::size_t size, SessionMessage& message);
} // namespace simnet::transport_protocol

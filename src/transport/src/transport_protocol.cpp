module;

#include <cstddef>
#include <cstdint>
#include <vector>

module simnet.transport;

import :protocol;

namespace simnet::transport_protocol
{
    namespace
    {
        constexpr std::uint32_t session_magic = 0x534E5453U;
        constexpr std::uint16_t session_version = 3;
        constexpr std::uint32_t session_header_bytes = 11;
    } // namespace

    DisconnectCode
    identity_mismatch(SessionIdentity const& actual, SessionIdentity const& expected) noexcept
    {
        if (actual.application_protocol_version != expected.application_protocol_version) {
            return DisconnectCode::ProtocolMismatch;
        }
        if (actual.compatibility_fingerprint != expected.compatibility_fingerprint) {
            return DisconnectCode::IncompatibleConfig;
        }
        if (actual.application_wire_fingerprint != expected.application_wire_fingerprint) {
            return DisconnectCode::IncompatibleWireProfile;
        }
        if (actual.capabilities != expected.capabilities) {
            return DisconnectCode::UnsupportedCapability;
        }
        return DisconnectCode::None;
    }

    std::uint8_t lane_index(TransportLane lane) noexcept
    {
        return static_cast<std::uint8_t>(lane);
    }

    bool valid_lane(TransportLane lane) noexcept
    {
        return lane_index(lane) < channel_count;
    }

    bool valid_delivery(Delivery delivery) noexcept
    {
        switch (delivery) {
            case Delivery::ReliableSequenced:
            case Delivery::UnreliableSequenced:
            case Delivery::UnreliableUnsequenced:
            case Delivery::UnreliableFragmented:
                return true;
        }
        return false;
    }

    bool valid_disconnect_code(DisconnectCode code) noexcept
    {
        switch (code) {
            case DisconnectCode::None:
            case DisconnectCode::Timeout:
            case DisconnectCode::ProtocolMismatch:
            case DisconnectCode::IncompatibleConfig:
            case DisconnectCode::IncompatibleWireProfile:
            case DisconnectCode::UnsupportedCapability:
            case DisconnectCode::ServerFull:
            case DisconnectCode::Rejected:
            case DisconnectCode::TransportError:
                return true;
        }
        return false;
    }

    void write_u8(std::vector<Byte>& bytes, std::uint8_t value)
    {
        bytes.push_back(static_cast<Byte>(value));
    }

    void write_u16(std::vector<Byte>& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
        bytes.push_back(static_cast<Byte>(value & 0xFFU));
    }

    void write_u32(std::vector<Byte>& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<Byte>((value >> 24U) & 0xFFU));
        bytes.push_back(static_cast<Byte>((value >> 16U) & 0xFFU));
        bytes.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
        bytes.push_back(static_cast<Byte>(value & 0xFFU));
    }

    void write_u64(std::vector<Byte>& bytes, std::uint64_t value)
    {
        write_u32(bytes, static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFULL));
        write_u32(bytes, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    }

    bool read_u8(Byte const* data, std::size_t size, std::size_t& offset, std::uint8_t& value)
    {
        if (offset + 1U > size) {
            return false;
        }
        value = static_cast<std::uint8_t>(data[offset]);
        ++offset;
        return true;
    }

    bool read_u16(Byte const* data, std::size_t size, std::size_t& offset, std::uint16_t& value)
    {
        std::uint8_t high{};
        std::uint8_t low{};
        if (!read_u8(data, size, offset, high) || !read_u8(data, size, offset, low)) {
            return false;
        }
        value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8U) | low);
        return true;
    }

    bool read_u32(Byte const* data, std::size_t size, std::size_t& offset, std::uint32_t& value)
    {
        std::uint8_t a{};
        std::uint8_t b{};
        std::uint8_t c{};
        std::uint8_t d{};
        if (!read_u8(data, size, offset, a) || !read_u8(data, size, offset, b)
            || !read_u8(data, size, offset, c) || !read_u8(data, size, offset, d)) {
            return false;
        }
        value = (static_cast<std::uint32_t>(a) << 24U) | (static_cast<std::uint32_t>(b) << 16U)
            | (static_cast<std::uint32_t>(c) << 8U) | static_cast<std::uint32_t>(d);
        return true;
    }

    bool read_u64(Byte const* data, std::size_t size, std::size_t& offset, std::uint64_t& value)
    {
        std::uint32_t high{};
        std::uint32_t low{};
        if (!read_u32(data, size, offset, high) || !read_u32(data, size, offset, low)) {
            return false;
        }
        value = (static_cast<std::uint64_t>(high) << 32U) | low;
        return true;
    }

    std::vector<Byte> encode_session_message(SessionMessage const& message)
    {
        auto payload = std::vector<Byte>{};
        if (message.kind == SessionMessageKind::ClientHello) {
            write_u32(payload, message.identity.application_protocol_version);
            write_u64(payload, message.identity.compatibility_fingerprint);
            write_u64(payload, message.identity.application_wire_fingerprint);
            write_u32(payload, message.identity.capabilities);
        } else if (message.kind == SessionMessageKind::ServerReject) {
            write_u16(payload, static_cast<std::uint16_t>(message.reject_code));
        }

        auto bytes = std::vector<Byte>{};
        bytes.reserve(session_header_bytes + payload.size());
        write_u32(bytes, session_magic);
        write_u16(bytes, session_version);
        write_u8(bytes, static_cast<std::uint8_t>(message.kind));
        write_u32(bytes, static_cast<std::uint32_t>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return bytes;
    }

    bool decode_session_message(Byte const* data, std::size_t size, SessionMessage& message)
    {
        std::size_t offset{};
        std::uint32_t magic{};
        std::uint16_t version{};
        std::uint8_t kind{};
        std::uint32_t payload_size{};
        if (!read_u32(data, size, offset, magic) || !read_u16(data, size, offset, version)
            || !read_u8(data, size, offset, kind) || !read_u32(data, size, offset, payload_size)) {
            return false;
        }
        if (magic != session_magic || version != session_version || offset + payload_size != size) {
            return false;
        }

        message.kind = static_cast<SessionMessageKind>(kind);
        if (message.kind == SessionMessageKind::ClientHello) {
            return payload_size == 24U
                && read_u32(data, size, offset, message.identity.application_protocol_version)
                && read_u64(data, size, offset, message.identity.compatibility_fingerprint)
                && read_u64(data, size, offset, message.identity.application_wire_fingerprint)
                && read_u32(data, size, offset, message.identity.capabilities);
        }
        if (message.kind == SessionMessageKind::ServerAccept) {
            return payload_size == 0U;
        }
        if (message.kind == SessionMessageKind::ServerReject) {
            auto code = std::uint16_t{};
            if (payload_size != 2U || !read_u16(data, size, offset, code)) {
                return false;
            }
            message.reject_code = static_cast<DisconnectCode>(code);
            return valid_disconnect_code(message.reject_code)
                && message.reject_code != DisconnectCode::None;
        }
        return false;
    }
} // namespace simnet::transport_protocol

module;

#include <cstddef>
#include <cstdint>
#include <vector>

module simnet.transport;

import :protocol;
import simnet.core;

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
        if (actual.application_protocol_version != expected.application_protocol_version)
        {
            return DisconnectCode::ProtocolMismatch;
        }
        if (actual.compatibility_fingerprint != expected.compatibility_fingerprint)
        {
            return DisconnectCode::IncompatibleConfig;
        }
        if (actual.application_wire_fingerprint != expected.application_wire_fingerprint)
        {
            return DisconnectCode::IncompatibleWireProfile;
        }
        if (actual.capabilities != expected.capabilities)
        {
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

    bool valid_delivery(TransportDelivery delivery) noexcept
    {
        switch (delivery)
        {
            case TransportDelivery::ReliableSequenced:
            case TransportDelivery::UnreliableSequenced:
                return true;
        }
        return false;
    }

    bool valid_disconnect_code(DisconnectCode code) noexcept
    {
        switch (code)
        {
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

    std::vector<Byte> encode_session_message(SessionMessage const& message)
    {
        auto payload = std::vector<Byte>{};
        if (message.kind == SessionMessageKind::ClientHello)
        {
            append_big_endian(payload, message.identity.application_protocol_version);
            append_big_endian(payload, message.identity.compatibility_fingerprint);
            append_big_endian(payload, message.identity.application_wire_fingerprint);
            append_big_endian(payload, message.identity.capabilities);
        }
        else if (message.kind == SessionMessageKind::ServerReject)
        {
            append_big_endian(payload, static_cast<std::uint16_t>(message.reject_code));
        }

        auto bytes = std::vector<Byte>{};
        bytes.reserve(session_header_bytes + payload.size());
        append_big_endian(bytes, session_magic);
        append_big_endian(bytes, session_version);
        append_byte(bytes, static_cast<std::uint8_t>(message.kind));
        append_big_endian(bytes, static_cast<std::uint32_t>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return bytes;
    }

    bool decode_session_message(ByteSpan bytes, SessionMessage& message)
    {
        std::size_t offset{};
        std::uint32_t magic{};
        std::uint16_t version{};
        std::uint8_t kind{};
        std::uint32_t payload_size{};
        if (!read_big_endian(bytes, offset, magic) || !read_big_endian(bytes, offset, version) ||
            !read_byte(bytes, offset, kind) || !read_big_endian(bytes, offset, payload_size))
        {
            return false;
        }
        if (magic != session_magic || version != session_version ||
            payload_size != bytes.size() - offset)
        {
            return false;
        }

        message.kind = static_cast<SessionMessageKind>(kind);
        if (message.kind == SessionMessageKind::ClientHello)
        {
            return payload_size == 24U &&
                   read_big_endian(bytes, offset, message.identity.application_protocol_version) &&
                   read_big_endian(bytes, offset, message.identity.compatibility_fingerprint) &&
                   read_big_endian(bytes, offset, message.identity.application_wire_fingerprint) &&
                   read_big_endian(bytes, offset, message.identity.capabilities);
        }
        if (message.kind == SessionMessageKind::ServerAccept)
        {
            return payload_size == 0U;
        }
        if (message.kind == SessionMessageKind::ServerReject)
        {
            auto code = std::uint16_t{};
            if (payload_size != 2U || !read_big_endian(bytes, offset, code))
            {
                return false;
            }
            message.reject_code = static_cast<DisconnectCode>(code);
            return valid_disconnect_code(message.reject_code) &&
                   message.reject_code != DisconnectCode::None;
        }
        return false;
    }
} // namespace simnet::transport_protocol

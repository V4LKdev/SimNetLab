#pragma once
#include <cstdint>
#include <cstddef>

/**
 *  Central definition of the network protocol's unchanging elements.
 *
 *  Everything defined is part of the wire format and changes must be accompanied with a protocol version bump.
 */
namespace simnet::net {
    using ProtocolVersion = std::uint16_t;

    /// Protocol version
    constexpr ProtocolVersion CURRENT_PROTOCOL_VERSION = 1;

    /// Message type identifiers used in packet headers
    enum class MessageType : uint8_t {
        Invalid = 0, /// Invalid or uninitialized message type

        Hello = 1, /// Client-initiated handshake message
        Welcome = 2, /// Server handshake acceptance message
        Reject = 3, /// Server handshake rejection message

        Ping = 10, /// Used for latency measurement and connection health
        Pong = 11, /// Response to ping, to measure rtt

        // Input = 20, /// Client side input commands
        // InputAck = 21, /// Server acknolwledgement of least received input command
        //
        Snapshot = 30, /// Server to client authoritative world state update
        // StateSync
        // ResyncRequest
        // Debug
    };

    enum class RejectReason : uint8_t {
        VersionMismatch = 1,
        ServerFull = 10,

        Kicked = 20,
        Banned = 21,

        Other = 99
    };

    enum class DisconnectReason : uint8_t {
        ServerClosed = 1,
        ClientQuit = 2,

        Timeout = 10,

        Other = 99
    };

    enum class SnapshotFlags : uint16_t {
        FullSnapshot = 1 << 0,
        Deltas = 1 << 1,
        Quantized = 1 << 2,
        // etc...
    };

    constexpr SnapshotFlags operator|(SnapshotFlags lhs, SnapshotFlags rhs) noexcept
    {
        return static_cast<SnapshotFlags>(static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs));
    }

    constexpr bool operator&(SnapshotFlags lhs, SnapshotFlags rhs) noexcept
    {
        return (static_cast<std::uint16_t>(lhs) & static_cast<std::uint16_t>(rhs)) != 0;
    }


    /// Fixed-size message header
    struct MessageHeader {
        MessageType type;
    };

    /// Fixed-size header that precedes every snapshot payload
    struct SnapshotHeader {
        uint32_t tick; // sim tick this snapshot represents
        uint32_t sequence; // monotonically increasing snapshot sequence number
        uint32_t baseline_tick; // delta base tick (0 when FullSnapshot)
        uint16_t flags; // SnapshotFlags bitmask
        uint32_t entity_count; // number of entities in this snapshot
    };

    struct HelloPayload {
        ProtocolVersion version;
    };

    struct RejectPayload {
        RejectReason reason;
    };

    struct DisconnectPayload {
        DisconnectReason reason;
    };

    constexpr std::size_t PACKET_HEADER_SIZE =
            sizeof(MessageType);
    static_assert(sizeof(MessageHeader) == PACKET_HEADER_SIZE);

    constexpr std::size_t HELLO_SIZE =
            sizeof(ProtocolVersion);
    static_assert(sizeof(HelloPayload) == HELLO_SIZE);

    constexpr std::size_t REJECT_SIZE =
            sizeof(RejectReason);
    static_assert(sizeof(RejectPayload) == REJECT_SIZE);

    constexpr std::size_t SNAPSHOT_HEADER_SIZE =
            sizeof(uint32_t) + // tick
            sizeof(uint32_t) + // sequence
            sizeof(uint32_t) + // baseline tick
            sizeof(uint16_t) + // flags
            sizeof(uint32_t); // entity count
}

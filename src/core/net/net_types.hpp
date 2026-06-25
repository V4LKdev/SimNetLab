#pragma once
#include <cstdint>
#include <cstddef>
#include "core/config/sim_config.hpp"

/**
 *  Central definition of the network protocol's unchanging elements.
 *
 *  Everything defined is part of the wire format and changes must be accompanied with a protocol version bump.
 */
namespace simnet::core::net::internal {
    using ProtocolVersion = std::uint16_t;
    using PeerID = uint32_t;

    // --- Protocol version -------------------------------
    constexpr ProtocolVersion CURRENT_PROTOCOL_VERSION = 2;
    // ----------------------------------------------------

    enum class NetChannel : uint8_t {
        System = 0, // handshake and system messages (reliable)
        GameEvent = 1, // gameplay events (reliable)
        Snapshot = 2, // game state snapshots (unreliable)
        Input = 3 // client inputs (unreliable)
    };

    constexpr uint8_t NUM_NET_CHANNELS = 4;


    /// Message type identifiers used in packet headers
    enum class MessageType : uint8_t {
        Invalid = 0, /// Invalid or uninitialized message type

        Hello = 1, /// Client-initiated handshake message
        Welcome = 2, /// Server handshake acceptance message
        Reject = 3, /// Server handshake rejection message


        // Input = 20, /// Client side input commands
        // InputAck = 21, /// Server acknolwledgement of least received input command

        Snapshot = 30, /// Server to client authoritative world state update
        // ConfigUpdate = 40, /// Server to client on config hotreload
        // StateSync
        // ResyncRequest
        // Debug
    };

    /// Handshake / Request rejection reason type
    enum class RejectReason : uint8_t {
        VersionMismatch = 1,
        ConfigMismatch = 2,
        ServerFull = 10,

        Kicked = 20,
        Banned = 21,

        Other = 99
    };

    /// Disconnect reason type
    enum class DisconnectReason : uint8_t {
        ServerClosed = 1,
        ClientQuit = 2,

        Timeout = 10,

        Rejected = 22,
        Other = 99
    };

    enum class SnapshotFlags : uint16_t {
        None = 0,
        FullSnapshot = 1 << 0, // complete baseline
        DeltaSnapshot = 1 << 1, // diff against a given baseline_tick
        IncrementalSnapshot = 1 << 2 // only entities that changed
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

    /// Fixed-size snapshot header (comes after message header, and before snapshot payload)
    struct SnapshotHeader {
        uint32_t tick; // sim tick this snapshot represents
        uint32_t sequence; // monotonically increasing snapshot sequence number
        uint32_t baseline_tick; // delta base tick (0 when FullSnapshot)
        uint16_t flags; // SnapshotFlags bitmask
        uint32_t entity_count; // number of entities in this snapshot
    };

    struct HelloPayload {
        ProtocolVersion version;
        /// Confirm server and client operate on the same config (until config send / hotreload are in)
        uint64_t config_fingerprint;
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
            sizeof(ProtocolVersion) +
            sizeof(uint64_t); // config fingerprint
    // No assert due to padding

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

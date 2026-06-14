#pragma once
#include <cstdint>

namespace simnet::net {
    using ProtocolVersion = std::uint16_t;

    enum class MessageType : uint8_t {
        Invalid = 0, /// Invalid or uninitialized message type

        Hello = 1, /// Client-initiated handshake message
        Welcome = 2, /// Server handshake acceptance message
        Reject = 3, /// Server handshake rejection message

        Ping = 10, /// Used for latency measurement and connection health
        Pong = 11, /// Response to ping, to measure rtt
        Disconnect = 12, /// Clean shutdown message before one side closes the connection

        // Input = 20, /// Client side input commands
        // InputAck = 21, /// Server acknolwledgement of least received input command
        //
        // Snapshot = 30, /// Server to client authoritative world state update
        // StateSync
        // ResyncRequest
        // Debug
    };
}

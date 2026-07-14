module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

/// @brief Backend-neutral transport contracts.
export module simnet.transport:types;

import simnet.core;

export namespace simnet
{
    enum class Lane : std::uint8_t
    {
        Control = 0,
        Snapshot = 1,
        Input = 2
    };

    enum class Delivery : std::uint8_t
    {
        ReliableSequenced,
        UnreliableSequenced,
        UnreliableUnsequenced,
        UnreliableFragmented
    };

    enum class SendSizePolicy : std::uint8_t
    {
        EnforceLimit,
        AllowBackendFragmentation
    };

    enum class DisconnectCode : std::uint16_t
    {
        None,
        Timeout,
        ProtocolMismatch,
        IncompatibleConfig,
        IncompatibleWireProfile,
        UnsupportedCapability,
        ServerFull,
        Rejected,
        TransportError
    };

    enum class TransportErrorCode : std::uint8_t
    {
        None,
        NotStarted,
        AlreadyStarted,
        InvalidAddress,
        ConnectionFailed,
        PeerNotFound,
        PeerNotReady,
        InvalidLane,
        InvalidDelivery,
        PayloadTooLarge,
        BackendError
    };

    struct SessionIdentity
    {
        std::uint32_t application_protocol_version {};
        std::uint64_t compatibility_fingerprint {};
        std::uint64_t pipeline_decode_signature {};
        std::uint32_t capabilities {};
    };

    struct TransportLimits
    {
        std::uint32_t max_payload_bytes { 1200 };
        SendSizePolicy size_policy { SendSizePolicy::EnforceLimit };
    };

    struct TransportError
    {
        TransportErrorCode code { TransportErrorCode::None };
        std::string message {};
        std::uint32_t native_code {};
    };

    struct TransportResult
    {
        bool ok { true };
        TransportError error {};
    };

    struct SendPacket
    {
        PeerId peer {};
        Lane lane { Lane::Snapshot };
        Delivery delivery { Delivery::UnreliableSequenced };
        std::span<Byte const> payload {};
    };

    struct ReceivedPacket
    {
        PeerId peer {};
        Lane lane {};
        Delivery delivery {};
        std::vector<Byte> payload;
    };

    struct PeerConnected
    {
        PeerId peer {};
    };

    struct PeerSessionReady
    {
        PeerId peer {};
    };

    struct PeerDisconnected
    {
        PeerId peer {};
        DisconnectCode code {};
        std::uint32_t native_reason {};
    };

    /// Cumulative semantic acknowledgement of decoded and applied snapshots.
    struct SnapshotAck
    {
        SequenceId newest_received_snapshot {};
        std::uint32_t received_mask {};
        SequenceId newest_applied_snapshot {};
    };

    struct SnapshotAckReceived
    {
        PeerId peer {};
        SnapshotAck ack {};
    };

    struct TransportErrorEvent
    {
        std::string message {};
    };

    using TransportEvent = std::variant<
        PeerConnected,
        PeerSessionReady,
        PeerDisconnected,
        SnapshotAckReceived,
        ReceivedPacket,
        TransportErrorEvent
    >;

    struct TransportStats
    {
        std::uint64_t packets_sent {};
        std::uint64_t bytes_sent {};
        std::uint64_t packets_received {};
        std::uint64_t bytes_received {};
        std::uint64_t send_failures {};
        std::uint64_t oversize_drops {};
        std::uint64_t disconnects {};
        std::array<std::uint64_t, 3> lane_packets_sent {};
        std::array<std::uint64_t, 3> lane_packets_received {};
        std::array<std::uint64_t, 3> lane_bytes_sent {};
        std::array<std::uint64_t, 3> lane_bytes_received {};
    };

    struct PeerStats
    {
        double rtt_ms {};
        double packet_loss_ratio {};
        std::uint64_t reliable_bytes_in_flight {};
        std::uint64_t waiting_bytes {};
        std::uint32_t mtu {};
    };
}

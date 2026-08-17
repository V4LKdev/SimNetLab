module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

/// @brief ENet transport contracts.
export module simnet.transport:types;

import simnet.core;

export namespace simnet
{
    /// Transport lanes map to ENet channels and therefore inherit ENet ordering limits.
    enum class TransportLane : std::uint8_t
    {
        Lane0 = 0,
        Lane1 = 1,
        Lane2 = 2
    };

    /// Delivery policy accepted for all post-handshake application traffic.
    enum class TransportDelivery : std::uint8_t
    {
        ReliableSequenced,
        UnreliableSequenced
    };

    /// Governs send-time size handling before ENet transmission.
    enum class SendSizePolicy : std::uint8_t
    {
        EnforceLimit,
        AllowBackendFragmentation
    };

    /// Disconnect classification surfaced to callers after session-level teardown events.
    enum class DisconnectCode : std::uint16_t
    {
        None,
        Timeout,
        ProtocolMismatch,
        IncompatibleConfig,
        IncompatibleWireProfile,
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

    /// Session identity contract that must match exactly to reach payload phase.
    struct SessionIdentity
    {
        std::uint32_t application_protocol_version{};
        std::uint64_t compatibility_fingerprint{};
        std::uint64_t application_wire_fingerprint{};
    };

    /// Send-time transport limits checked before ENet queueing or serialization.
    struct TransportLimits
    {
        std::uint32_t max_payload_bytes{1200};
        SendSizePolicy size_policy{SendSizePolicy::EnforceLimit};
    };

    /// Failure contract returned for lifecycle operations and transport send.
    struct TransportError
    {
        TransportErrorCode code{TransportErrorCode::None};
        std::string message{};
        std::uint32_t native_code{};
    };

    /// `ok` means no transport-level or lifecycle failure and `error` is stable contract text.
    struct TransportResult
    {
        bool ok{true};
        TransportError error{};
    };

    /// Outgoing payload owned by caller. Transport does not keep caller memory after call.
    struct SendPacket
    {
        PeerId peer{};
        TransportLane lane{TransportLane::Lane0};
        TransportDelivery delivery{TransportDelivery::UnreliableSequenced};
        std::span<Byte const> payload{};
    };

    /// Incoming application payload is copied on receive to keep transport ownership local.
    struct ReceivedPacket
    {
        PeerId peer{};
        TransportLane lane{};
        TransportDelivery delivery{};
        std::vector<Byte> payload;
    };

    /// Emitted for ENet connect events. Payload transport has not yet been validated by app logic.
    struct PeerConnected
    {
        PeerId peer{};
    };

    /// Emitted after version fingerprint session handshake succeeds.
    struct PeerSessionReady
    {
        PeerId peer{};
    };

    /// Reported before or after peer teardown. Disconnection side effects are transport-owned.
    struct PeerDisconnected
    {
        PeerId peer{};
        DisconnectCode code{};
        std::uint32_t native_reason{};
    };

    /// Non-fatal transport diagnostic. Transport call may continue afterward.
    struct TransportErrorEvent
    {
        std::string message{};
    };

    using TransportEvent = std::variant<
        PeerConnected,
        PeerSessionReady,
        PeerDisconnected,
        ReceivedPacket,
        TransportErrorEvent>;

    struct TransportStats
    {
        std::uint64_t packets_sent{};
        std::uint64_t bytes_sent{};
        std::uint64_t packets_received{};
        std::uint64_t bytes_received{};
        std::uint64_t send_failures{};
        std::uint64_t oversize_drops{};
        std::uint64_t disconnects{};
        std::array<std::uint64_t, 3> lane_packets_sent{};
        std::array<std::uint64_t, 3> lane_packets_received{};
        std::array<std::uint64_t, 3> lane_bytes_sent{};
        std::array<std::uint64_t, 3> lane_bytes_received{};
    };

    struct PeerStats
    {
        double rtt_ms{};
        double packet_loss_ratio{};
        std::uint64_t reliable_bytes_in_flight{};
        std::uint64_t waiting_bytes{};
        std::uint32_t mtu{};
    };
}

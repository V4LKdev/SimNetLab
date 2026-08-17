module;

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
    };

    /// Failure contract returned for lifecycle operations and transport send.
    struct TransportError
    {
        TransportErrorCode code{TransportErrorCode::None};
        std::string message{};
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

}

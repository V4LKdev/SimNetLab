module;

#include <cstdint>
#include <span>
#include <string>
#include <vector>

/// @brief Server-side transport role.
export module simnet.transport:server;

import :types;
import simnet.core;

export namespace simnet
{
    /// Thread-affine ENet server transport for handshake, session readiness, and payload relay.
    struct TransportServerSettings
    {
        std::string bind_address{};
        std::uint16_t port{};
        std::uint32_t max_peers{32};
        SessionIdentity expected_identity{};
        TransportLimits limits{};
    };

    /// Owns one ENet host and all server peer slots until `stop` or destruction.
    class TransportServer
    {
      public:
        TransportServer();
        ~TransportServer();

        TransportServer(TransportServer&&) noexcept;
        TransportServer& operator=(TransportServer&&) noexcept;

        TransportServer(TransportServer const&) = delete;
        TransportServer& operator=(TransportServer const&) = delete;

        /// Starts networking and session policy.
        /// Rejects if already started or settings are invalid.
        [[nodiscard]] TransportResult start(TransportServerSettings const& settings);

        /// Idempotent transport shutdown boundary.
        /// Safe to call on normal completion.
        void stop() noexcept;

        /// Server has an active ENet host.
        /// Caller keeps ownership on the owner thread.
        [[nodiscard]] bool is_running() const noexcept;

        /// Poll drains ENet events.
        /// Emits transport events in event order from ENet.
        [[nodiscard]] TransportResult
        poll(std::vector<TransportEvent>& out_events, std::uint32_t timeout_ms);

        /// Sends only to known peers.
        /// Send requests require peer session readiness.
        [[nodiscard]] TransportResult send(SendPacket const& packet);

        /// Requests disconnect from owner thread.
        /// Immediate transport path is best effort.
        void disconnect(PeerId peer, DisconnectCode code) noexcept;

      private:
        struct Impl;
        Impl* impl_{};
    };
}

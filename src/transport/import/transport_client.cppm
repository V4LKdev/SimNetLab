module;

#include <cstdint>
#include <span>
#include <string>
#include <vector>

/// @brief Client-side transport role.
export module simnet.transport:client;

import :types;

export namespace simnet
{
    /// Configures single-session ENet client ownership and handshake expectations.
    struct TransportClientSettings
    {
        std::string server_address{"127.0.0.1"};
        std::uint16_t server_port{};
        SessionIdentity identity{};
        TransportLimits limits{};
    };

    /// Thread-affine ENet client transport for one server connection and opaque payload flow.
    class TransportClient
    {
      public:
        TransportClient();
        ~TransportClient();

        TransportClient(TransportClient&&) noexcept;
        TransportClient& operator=(TransportClient&&) noexcept;

        TransportClient(TransportClient const&) = delete;
        TransportClient& operator=(TransportClient const&) = delete;

        /// Starts outbound connection and session request.
        /// Fails with lifecycle error on conflict.
        [[nodiscard]] TransportResult connect(TransportClientSettings const& settings);

        /// Requests disconnect and blocks briefly for ENet close event during teardown.
        void disconnect(DisconnectCode code) noexcept;

        /// Connected means ENet link exists.
        /// Session payload phase still requires readiness.
        [[nodiscard]] bool is_connected() const noexcept;

        /// Readiness is set only after explicit ServerAccept handshake message.
        [[nodiscard]] bool is_session_ready() const noexcept;

        /// Poll drains ordered ENet events.
        /// Updates readiness, disconnect, and reception state.
        [[nodiscard]] TransportResult
        poll(std::vector<TransportEvent>& out_events, std::uint32_t timeout_ms);

        /// Sends only after session is ready.
        /// Payload constraints are enforced before send queue.
        [[nodiscard]] TransportResult
        send(TransportLane lane, TransportDelivery delivery, std::span<Byte const> payload);

        /// Snapshot read for transport-owned counters and bytes.
        [[nodiscard]] TransportStats stats() const;

        /// Returns known server transport counters or zero state if session is not active.
        [[nodiscard]] PeerStats server_stats() const;

      private:
        struct Impl;
        Impl* impl_{};
    };
}

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
    struct TransportClientSettings
    {
        std::string server_address { "127.0.0.1" };
        std::uint16_t server_port {};
        SessionIdentity identity {};
        TransportLimits limits {};
    };

    class TransportClient
    {
    public:
        TransportClient();
        ~TransportClient();

        TransportClient(TransportClient&&) noexcept;
        TransportClient& operator=(TransportClient&&) noexcept;

        TransportClient(TransportClient const&) = delete;
        TransportClient& operator=(TransportClient const&) = delete;

        [[nodiscard]] TransportResult connect(TransportClientSettings const& settings);
        void disconnect(DisconnectCode code) noexcept;

        [[nodiscard]] bool is_connected() const noexcept;
        [[nodiscard]] bool is_session_ready() const noexcept;

        [[nodiscard]] TransportResult poll(
            std::vector<TransportEvent>& out_events,
            std::uint32_t timeout_ms
        );

        [[nodiscard]] TransportResult send(
            Lane lane,
            Delivery delivery,
            std::span<std::byte const> payload
        );

        /// Sends a semantic snapshot acknowledgement on the reserved Input lane.
        [[nodiscard]] TransportResult send_snapshot_ack(SnapshotAck const& ack);

        [[nodiscard]] TransportStats stats() const;
        [[nodiscard]] PeerStats server_stats() const;

    private:
        struct Impl;
        Impl* impl_ {};
    };
}

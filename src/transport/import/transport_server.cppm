module;

#include <cstdint>
#include <string>
#include <vector>

/// @brief Server-side transport role.
export module simnet.transport:server;

import :types;
import simnet.core;

export namespace simnet
{
    struct TransportServerSettings
    {
        TransportBackend backend { TransportBackend::ENet };
        std::string bind_address {};
        std::uint16_t port {};
        std::uint32_t max_peers { 32 };
        SessionIdentity expected_identity {};
        TransportLimits limits {};
    };

    class TransportServer
    {
    public:
        TransportServer();
        ~TransportServer();

        TransportServer(TransportServer&&) noexcept;
        TransportServer& operator=(TransportServer&&) noexcept;

        TransportServer(TransportServer const&) = delete;
        TransportServer& operator=(TransportServer const&) = delete;

        [[nodiscard]] TransportResult start(TransportServerSettings const& settings);
        void stop() noexcept;

        [[nodiscard]] bool is_running() const noexcept;

        [[nodiscard]] TransportResult poll(
            std::vector<TransportEvent>& out_events,
            std::uint32_t timeout_ms
        );

        [[nodiscard]] TransportResult send(SendPacket const& packet);

        void disconnect(PeerId peer, DisconnectCode code) noexcept;

        [[nodiscard]] TransportStats stats() const;
        [[nodiscard]] PeerStats peer_stats(PeerId peer) const;

    private:
        struct Impl;
        Impl* impl_ {};
    };
}

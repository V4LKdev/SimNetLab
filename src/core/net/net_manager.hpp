// net_manager.hpp
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "core/net/pipeline/net_pipeline_interfaces.hpp"
#include "core/net/pipeline/net_pipeline_chain.hpp"
#include "core/net/net_types.hpp"
#include "core/net/net_transport_interface.hpp"
#include "core/utils/time_keeper.hpp"

namespace simnet::core::net {
    // re‑exported types
    using PeerID = internal::PeerID;
    using DisconnectReason = internal::DisconnectReason;
    using RejectReason = internal::RejectReason;

    enum class NetRole { server, client };

    /**
     * @brief Central network facade.
     *
     * Owns transport, connection handler, and the pipeline chain.
     * The pipeline is built once from SimConfig flags and reused for every peer tick.
     */
    class NetManager {
    public:
        /**
        * @brief Initialize the network stack for a given role.
        * @param role Server or client.
        * @param config Port, max peers/channels, and timing settings.
        */
        NetManager(NetRole role, const config::SimConfig &config);

        ~NetManager();

        NetManager(const NetManager &) = delete;

        NetManager &operator=(const NetManager &) = delete;

        NetManager(NetManager &&) = delete;

        NetManager &operator=(NetManager &&) = delete;

        [[nodiscard]] bool is_initialized() const;

        /**
         * @brief Tear down the entire network stack and release resources.
         */
        void shutdown() const;

        /**
        * @brief Process incoming events and maintain connections.
        * @param now Monotonic timestamp for timeout/keep‑alive logic.
        */
        void update(utils::TimePoint now, int timeout_ms) const;

        /**
        * @brief (Client only) Initiate a connection to the remote host.
        * @param address Remote IP or hostname.
        * @param port Remote port.
        * @return PeerID of the new connection, or 0 on failure.
        */
        [[nodiscard]] PeerID connect(const std::string &address, uint16_t port) const;

        /**
        * @brief (Server only) Send the current snapshot to all handshaked clients.
        * @param snapshot The global SoA snapshot built by the ECS NetPrepareSnapshot system.
        */
        void broadcast_snapshot(const internal::NetworkSnapshot &snapshot) const;

        // Callbacks
        /// Called when a peer completes the handshake and is ready for gameplay.
        void set_on_connected(std::function<void(PeerID)> callback) const;

        /// Called when a peer disconnects, with reason.
        void set_on_disconnected(std::function<void(PeerID, DisconnectReason)> callback) const;

        /// Called when a connection attempt is rejected (e.g., full server, bad version).
        void set_on_rejected(std::function<void(PeerID, RejectReason)> callback) const;

        /// Called on each received full‑state snapshot (client only).
        void set_on_snapshot_received(std::function<void(const internal::NetworkSnapshot &)> callback) const;

        /// (server only) Get a list of all handshaked peer IDs.
        [[nodiscard]] std::vector<PeerID> get_connected_peer_ids() const;

        [[nodiscard]] uint64_t get_config_fingerprint() const;

        /**
         * @brief Inject a mock transport (for unit testing only).
         */
        void set_transport_for_testing(std::unique_ptr<internal::INetTransport> transport) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

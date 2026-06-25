// net_manager.hpp
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "pipeline/net_pipeline_interfaces.hpp"
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
     * Owns transport, connection handler, and the snapshot history.
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
        void shutdown();

        /**
        * @brief Process incoming events and maintain connections.
        * @param now Monotonic timestamp for timeout/keep‑alive logic.
        */
        void update(utils::TimePoint now, int timeout_ms);

        /**
        * @brief (Client only) Initiate a connection to the remote host.
        * @param address Remote IP or hostname.
        * @param port Remote port.
        * @return PeerID of the new connection, or 0 on failure.
        */
        [[nodiscard]] PeerID connect(const std::string &address, uint16_t port);

        /**
        * @brief (Server) Store a snapshot in the history ring for delta baselines.
        * @param snapshot The snapshot to store (copied into a shared_ptr).
        */
        void push_snapshot(const internal::NetworkSnapshot &snapshot);

        /**
         * @brief Retrieve a baseline snapshot from the history ring.
         * @param tick The tick of the desired snapshot.
         * @return A shared_ptr to the snapshot, or nullptr if not found.
         */
        [[nodiscard]] std::shared_ptr<const internal::NetworkSnapshot> get_baseline(uint64_t tick);

        /**
         * @brief Send a raw buffer to a specific peer.
         * @param peer Target peer ID.
         * @param buffer The buffer to send.
         * @param channel ENet channel (0-3).
         * @param reliability Reliability flag.
         */
        void send_to_peer(PeerID peer, const internal::NetBuffer &buffer,
                          uint8_t channel, internal::TransportReliability reliability);


        /**
         * @brief Get mutable reference to a peer's state.
         * @param id Peer ID.
         * @return Reference to internal::PeerState.
         * @throws std::out_of_range if peer not found.
         */
        [[nodiscard]] internal::PeerState &get_peer_state(PeerID id) const;

        // Callbacks
        /// Called when a peer completes the handshake and is ready for gameplay.
        void set_on_connected(std::function<void(PeerID)> callback);

        /// Called when a peer disconnects, with reason.
        void set_on_disconnected(std::function<void(PeerID, DisconnectReason)> callback);

        /// Called when a connection attempt is rejected (e.g., full server, bad version).
        void set_on_rejected(std::function<void(PeerID, RejectReason)> callback);

        /// Called on each received full‑state snapshot (client only).
        void set_on_snapshot_received(std::function<void(const internal::NetworkSnapshot &)> callback);

        /// (server only) Get a list of all handshaked peer IDs.
        [[nodiscard]] std::vector<PeerID> get_connected_peer_ids() const;

        [[nodiscard]] uint64_t get_config_fingerprint() const;

        /**
         * @brief Inject a mock transport (for unit testing only).
         */
        void set_transport_for_testing(std::unique_ptr<internal::INetTransport> transport);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

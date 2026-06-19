#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "net_snapshot.hpp"
#include "net_transport_interface.hpp"
#include "net_types.hpp"
#include "utils/time_keeper.hpp"

namespace simnet::core::net {
    namespace internal {
        class NetProcessor;
    }

    // Re-exported from internal for clean public API
    using PeerID = internal::PeerID;
    using DisconnectReason = internal::DisconnectReason;
    using RejectReason = internal::RejectReason;

    enum class NetRole { server, client };

    struct NetConfig {
        uint16_t port = 0;
        std::size_t max_peers = 0; // server only
        uint16_t max_channels = 0; // client only
        int ping_interval_ms = 2000;
        int timeout_ms = 5000;
    };

    using SnapshotCallback = std::function<void(const internal::ReplicationSnapshot &)>;

    /**
    * @brief Central network facade for client and server game instances.
    *
    * Hides transport, connection management, and packet processing pipelines
    * behind a minimal public API. Game code only includes this header and
    * uses callbacks to react to network events.
    *
    * @ingroup network
    */
    class NetManager {
    public:
        NetManager();

        ~NetManager();

        NetManager(const NetManager &) = delete;

        NetManager &operator=(const NetManager &) = delete;

        NetManager(NetManager &&) = delete;

        NetManager &operator=(NetManager &&) = delete;

        /**
        * @brief Initialize the network stack for a given role.
        * @param role Server or client.
        * @param config Port, max peers/channels, and timing settings.
        * @return true if ENet and transport started successfully.
        */
        [[nodiscard]]
        bool initialize(NetRole role, const NetConfig &config) const;

        /**
         * @brief Tear down the entire network stack and release resources.
         */
        void shutdown() const;

        /**
        * @brief Process incoming events and maintain connections.
        * @param now Monotonic timestamp for timeout/keep‑alive logic.
        */
        void update(utils::TimePoint now) const;

        /**
        * @brief (Client only) Initiate a connection to the remote host.
        * @param address Remote IP or hostname.
        * @param port Remote port.
        * @return PeerID of the new connection, or 0 on failure.
        */
        [[nodiscard]]
        PeerID connect(const std::string &address, uint16_t port) const;

        /**
        * @brief (Server only) Broadcast a full replicated snapshot to all handshaked clients.
        * @param snapshot Flat container with entity data and tick info.
        */
        void broadcast_snapshot(const internal::ReplicationSnapshot &snapshot) const;

        /**
        * @brief Register a network processing stage (delta, quantization, AoI, …).
        * @param processor Unique pointer; ownership transfers.
        */
        void add_processor(std::unique_ptr<internal::NetProcessor> processor) const;

        // Callbacks
        /// Called when a peer completes the handshake and is ready for gameplay.
        void set_on_connected(std::function<void(PeerID)> callback) const;

        /// Called when a peer disconnects, with reason.
        void set_on_disconnected(std::function<void(PeerID, DisconnectReason)> callback) const;

        /// Called when a connection attempt is rejected (e.g., full server, bad version).
        void set_on_rejected(std::function<void(PeerID, RejectReason)> callback) const;

        /// Called on each received full‑state snapshot.
        void set_snapshot_callback(SnapshotCallback callback) const;

        /**
         * @brief Inject a mock transport (for unit testing only).
         */
        void set_transport_for_testing(std::unique_ptr<internal::INetTransport> transport) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

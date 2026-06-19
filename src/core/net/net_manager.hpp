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
    // Forward declarations
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

    class NetManager {
    public:
        NetManager();

        ~NetManager();

        NetManager(const NetManager &) = delete;

        NetManager &operator=(const NetManager &) = delete;

        NetManager(NetManager &&) = delete;

        NetManager &operator=(NetManager &&) = delete;

        bool initialize(NetRole role, const NetConfig &config);

        void shutdown();

        void update(utils::TimePoint now);

        // Client only
        PeerID connect(const std::string &address, uint16_t port);

        // Server only
        void broadcast_snapshot(const internal::ReplicationSnapshot &snapshot);

        // Pipeline extension
        void add_processor(std::unique_ptr<internal::NetProcessor> processor);

        // Callbacks (types now directly usable without internal::)
        void set_on_connected(std::function<void(PeerID)> callback);

        void set_on_disconnected(std::function<void(PeerID, DisconnectReason)> callback);

        void set_on_rejected(std::function<void(PeerID, RejectReason)> callback);

        void set_snapshot_callback(SnapshotCallback callback);

        void set_transport_for_testing(std::unique_ptr<internal::INetTransport> transport);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

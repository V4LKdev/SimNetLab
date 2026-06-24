#include "net_manager.hpp"
#include "connection_handler.hpp"
#include "net_pipeline.hpp"
#include "net_transport.hpp"
#include "telemetry/telemetry.hpp"
#include <mutex>

namespace simnet::core::net {
    using namespace internal;

    struct NetManager::Impl {
        NetRole role = NetRole::client;
        config::SimConfig sim_config;
        std::unique_ptr<INetTransport> transport;
        std::unique_ptr<ConnectionHandler> conn_handler;
        std::unique_ptr<NetPipeline> pipeline;

        SnapshotCallback snapshot_callback;

        std::function<void(PeerID)> game_on_connected;
        std::function<void(PeerID, DisconnectReason)> game_on_disconnected;
        std::function<void(PeerID, RejectReason)> game_on_rejected;

        utils::TimePoint current_time;

        bool initialized = false;


        void dispatch_incoming(PeerID id, NetBuffer &buffer) const
        {
            TELEM_TRACY_ZONE_C("NetManager_dispatch_incoming", TELEM_COLOR_NET_RECV);

            if (buffer.size() < 1) {
                TELEM_LOG_WARN("NetManager: empty packet from peer {}", id);
                return;
            }

            size_t const incoming_size = buffer.size();

            // Peek message type
            auto msg_type = static_cast<MessageType>(buffer.read<uint8_t>());
            buffer.reset_data();

            if (msg_type == MessageType::Snapshot) {
                auto *peer = conn_handler->find_peer(id);
                if (!peer || !peer->is_handshake_complete()) {
                    TELEM_LOG_WARN("NetManager: Snapshot from non-handshaked or missing peer {}", id);
                    TELEM_COUNTER_INC("net.snapshot_rejected_no_handshake", 1);
                    return;
                }
                auto &peer_state = *peer;

                pipeline->apply_incoming(peer_state, buffer, SnapshotFlags(0));
                buffer.reset_data();


                try {
                    ReplicationSnapshot snap = ReplicationSnapshot::read(buffer);
                    if (snapshot_callback) {
                        snapshot_callback(snap);
                    }

                    // Telemetry
                    TELEM_COUNTER_INC("net.snapshot_recv", 1);
                    TELEM_HISTOGRAM_ADD("net.snapshot_in_size", static_cast<double>(incoming_size));
                } catch (const std::exception &e) {
                    TELEM_LOG_WARN("NetManager: Failed to parse snapshot from peer {}: {}", id, e.what());
                    TELEM_COUNTER_INC("net.snapshot_parse_error", 1);
                }
            } else {
                conn_handler->on_transport_data(id, buffer);
            }
        }
    };

    NetManager::NetManager(NetRole role, const config::SimConfig &config)
        : impl_(std::make_unique<Impl>())
    {
        if (impl_->initialized) {
            return;
        }

        impl_->sim_config = config;

        TELEM_LOG_INFO("NetManager: Initializing as {}", (role == NetRole::server ? "server" : "client"));

        if (enet_initialize() != 0) {
            TELEM_LOG_ERROR("NetManager: enet_initialize failed");
            return;
        }

        impl_->role = role;
        if (!impl_->transport) {
            impl_->transport = std::make_unique<NetTransport>();
        }
        impl_->pipeline = std::make_unique<NetPipeline>();

        bool ok = false;
        if (role == NetRole::server) {
            ok = impl_->transport->initialize_server(config.net_port, config.net_max_peers);
        } else {
            ok = impl_->transport->initialize_client();
        }

        if (!ok) {
            TELEM_LOG_ERROR("NetManager: Failed to initialize transport");
            impl_->transport.reset();
            impl_->pipeline.reset();
            enet_deinitialize();
            return;
        }

        impl_->conn_handler = std::make_unique<ConnectionHandler>(*impl_->transport, impl_->sim_config);
        impl_->conn_handler->set_role(role == NetRole::server);

        // Wire ConnectionHandler callbacks
        impl_->conn_handler->on_connected = [this](PeerID id) {
            if (impl_->game_on_connected) impl_->game_on_connected(id);
        };
        impl_->conn_handler->on_disconnected = [this](PeerID id, DisconnectReason reason) {
            if (impl_->game_on_disconnected) impl_->game_on_disconnected(id, reason);
        };
        impl_->conn_handler->on_rejected = [this](PeerID id, RejectReason reason) {
            if (impl_->game_on_rejected) impl_->game_on_rejected(id, reason);
        };

        // Wire transport callbacks
        TransportCallbacks tcb;
        tcb.on_new_connection = [this](PeerID id) {
            impl_->conn_handler->on_transport_connect(id);
        };
        tcb.on_disconnection = [this](PeerID id, DisconnectReason reason) {
            impl_->conn_handler->on_transport_disconnect(id, reason);
        };
        tcb.on_data = [this](PeerID id, NetBuffer &buffer) {
            impl_->dispatch_incoming(id, buffer);
        };
        impl_->transport->set_callbacks(std::move(tcb));

        impl_->initialized = true;
    }

    NetManager::~NetManager() { shutdown(); }

    bool NetManager::is_initialized() const
    {
        if (!impl_->initialized) return false;
        return impl_->initialized;
    }

    static std::once_flag enet_deinit_flag;

    void NetManager::shutdown() const
    {
        if (!impl_->initialized) return;

        TELEM_LOG_INFO("NetManager: Shutting down");

        impl_->transport->shutdown();
        impl_->conn_handler.reset();
        impl_->pipeline.reset();
        impl_->transport.reset();
        impl_->initialized = false;

        std::call_once(enet_deinit_flag, [] {
            TELEM_LOG_DEBUG("NetManager: Deinitializing ENet");
            enet_deinitialize();
        });
    }

    void NetManager::update(utils::TimePoint now, int timeout_ms) const
    {
        if (!impl_->initialized) return;
        impl_->current_time = now;
        impl_->transport->service(now, timeout_ms);
        impl_->conn_handler->update(now);
    }


    PeerID NetManager::connect(const std::string &address, uint16_t port) const
    {
        if (!impl_->initialized || impl_->role != NetRole::client) return 0;

        TELEM_LOG_INFO("NetManager: Connecting to {}:{}", address, port);
        PeerID id = impl_->transport->connect(address, port);
        if (id != 0) {
            impl_->conn_handler->register_outgoing_peer(id);
            TELEM_COUNTER_INC("net.connection_attempts", 1);
        }
        return id;
    }


    void NetManager::broadcast_snapshot(const ReplicationSnapshot &snapshot) const
    {
        TELEM_TRACY_ZONE_C("NetManager_broadcast_snapshot", TELEM_COLOR_NET_SEND);

        if (!impl_->initialized || impl_->role != NetRole::server) return;

        TELEM_LOG_DEBUG("NetManager: Broadcasting snapshot (tick {}, {} entities)",
                        snapshot.tick, snapshot.entities.size());

        auto peer_ids = impl_->conn_handler->get_connected_peer_ids();

        TELEM_TRACY_PLOT("net.broadcast_peer_count", static_cast<int64_t>(peer_ids.size()));

        for (PeerID id: peer_ids) {
            auto &peer_state = impl_->conn_handler->get_peer_state(id);
            if (!peer_state.is_handshake_complete()) {
                continue;
            }
            NetBuffer buffer;
            snapshot.write(buffer);

            // Useful for later
            size_t const size_before_pipeline = buffer.size();

            impl_->pipeline->apply_outgoing(impl_->conn_handler->get_peer_state(id), buffer,
                                            SnapshotFlags::FullSnapshot);

            // --- Telemetry ---
            TELEM_COUNTER_INC("net.snapshot_sent_per_peer", 1);
            TELEM_HISTOGRAM_ADD("net.snapshot_out_size", static_cast<double>(buffer.size()));

            impl_->transport->send(id, buffer,
                                   static_cast<uint8_t>(NetChannel::Snapshot),
                                   TransportReliability::unreliable_fragmented);
        }

        TELEM_COUNTER_INC("net.snapshot_sent_total", static_cast<int64_t>(peer_ids.size()));
    }


    void NetManager::add_processor(std::unique_ptr<NetProcessor> processor) const
    {
        impl_->pipeline->add_processor(std::move(processor));
    }

    void NetManager::set_on_connected(std::function<void(PeerID)> callback) const
    {
        impl_->game_on_connected = std::move(callback);
    }

    void NetManager::set_on_disconnected(std::function<void(PeerID, DisconnectReason)> callback) const
    {
        impl_->game_on_disconnected = std::move(callback);
    }

    void NetManager::set_on_rejected(std::function<void(PeerID, RejectReason)> callback) const
    {
        impl_->game_on_rejected = std::move(callback);
    }

    void NetManager::set_snapshot_callback(SnapshotCallback callback) const
    {
        impl_->snapshot_callback = std::move(callback);
    }

    std::vector<PeerID> NetManager::get_connected_peer_ids() const
    {
        if (!impl_->conn_handler) return {};
        return impl_->conn_handler->get_connected_peer_ids();
    }

    uint64_t NetManager::get_config_fingerprint() const
    {
        return impl_->sim_config.fingerprint();
    }

    void NetManager::set_transport_for_testing(std::unique_ptr<INetTransport> transport) const
    {
        impl_->transport = std::move(transport);
        impl_->conn_handler->set_transport(*impl_->transport);

        // Re‑apply callbacks
        TransportCallbacks tcb;
        tcb.on_new_connection = [this](PeerID id) {
            impl_->conn_handler->on_transport_connect(id);
        };
        tcb.on_disconnection = [this](PeerID id, DisconnectReason reason) {
            impl_->conn_handler->on_transport_disconnect(id, reason);
        };
        tcb.on_data = [this](PeerID id, NetBuffer &buffer) {
            impl_->dispatch_incoming(id, buffer);
        };
        impl_->transport->set_callbacks(std::move(tcb));
    }
}

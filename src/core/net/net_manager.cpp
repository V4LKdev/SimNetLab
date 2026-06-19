#include "net_manager.hpp"
#include "connection_handler.hpp"
#include "net_pipeline.hpp"
#include "real_net_transport.hpp"
#include "telemetry.hpp"

namespace simnet::core::net {
    using namespace internal;

    struct NetManager::Impl {
        NetRole role = NetRole::client;
        std::unique_ptr<INetTransport> transport;
        std::unique_ptr<ConnectionHandler> conn_handler;
        std::unique_ptr<NetPipeline> pipeline;

        SnapshotCallback snapshot_callback;

        std::function<void(PeerID)> game_on_connected;
        std::function<void(PeerID, DisconnectReason)> game_on_disconnected;
        std::function<void(PeerID, RejectReason)> game_on_rejected;

        utils::TimePoint current_time;

        bool initialized = false;


        void dispatch_incoming(PeerID id, NetBuffer &buffer)
        {
            TELEM_TRACY_ZONE_C("NetManager_dispatch_incoming", TELEM_COLOR_NET_RECV);

            if (buffer.size() < 1) {
                TELEM_LOG_WARN("NetManager: empty packet from peer {}", id);
                return;
            }

            // Peek message type
            auto msg_type = static_cast<MessageType>(buffer.read<uint8_t>());
            buffer.reset_data();

            if (msg_type == MessageType::Snapshot) {
                auto *peer = conn_handler->find_peer(id);
                if (!peer || !peer->is_handshake_complete()) {
                    TELEM_LOG_WARN("NetManager: Snapshot from non-handshaked or missing peer {}", id);
                    return;
                }
                auto &peer_state = *peer;

                conn_handler->record_peer_activity(id, current_time);
                pipeline->apply_incoming(peer_state, buffer, SnapshotFlags(0));
                buffer.reset_data();


                try {
                    ReplicationSnapshot snap = ReplicationSnapshot::read(buffer);
                    if (snapshot_callback) {
                        snapshot_callback(snap);
                    }
                } catch (const std::exception &e) {
                    TELEM_LOG_WARN("NetManager: Failed to parse snapshot from peer {}: {}", id, e.what());
                }
            } else {
                conn_handler->on_transport_data(id, buffer);
            }
        }
    };

    NetManager::NetManager() : impl_(std::make_unique<Impl>())
    {
    }

    NetManager::~NetManager() { shutdown(); }

    bool NetManager::initialize(NetRole role, const NetConfig &config)
    {
        if (impl_->initialized) return false;

        TELEM_LOG_INFO("NetManager: Initializing as {}", (role == NetRole::server ? "server" : "client"));

        if (enet_initialize() != 0) {
            TELEM_LOG_ERROR("NetManager: enet_initialize failed");
            return false;
        }

        impl_->role = role;
        // if a mock is set for testing, preserve
        if (!impl_->transport) {
            impl_->transport = std::make_unique<RealNetTransport>();
        }
        impl_->pipeline = std::make_unique<NetPipeline>();

        bool ok = false;
        if (role == NetRole::server) {
            ok = impl_->transport->initialize_server(config.port, config.max_peers);
        } else {
            ok = impl_->transport->initialize_client(config.max_channels);
        }

        if (!ok) {
            TELEM_LOG_ERROR("NetManager: Failed to initialize transport");
            impl_->transport.reset();
            impl_->pipeline.reset();
            enet_deinitialize();
            return false;
        }

        utils::Milliseconds ping_interval(config.ping_interval_ms);
        utils::Milliseconds timeout(config.timeout_ms);
        impl_->conn_handler = std::make_unique<ConnectionHandler>(*impl_->transport, ping_interval, timeout);
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
        return true;
    }

    void NetManager::shutdown()
    {
        if (!impl_->initialized) return;

        TELEM_LOG_INFO("NetManager: Shutting down");

        impl_->transport->shutdown();
        impl_->conn_handler.reset();
        impl_->pipeline.reset();
        impl_->transport.reset();
        enet_deinitialize();
        impl_->initialized = false;
    }

    void NetManager::update(utils::TimePoint now)
    {
        if (!impl_->initialized) return;
        impl_->current_time = now;
        impl_->transport->service(now);
        impl_->conn_handler->update(now);
    }


    PeerID NetManager::connect(const std::string &address, uint16_t port)
    {
        if (!impl_->initialized || impl_->role != NetRole::client) return 0;

        TELEM_LOG_INFO("NetManager: Connecting to {}:{}", address, port);
        PeerID id = impl_->transport->connect(address, port);
        if (id != 0) {
            impl_->conn_handler->register_outgoing_peer(id);
        }
        return id;
    }


    void NetManager::broadcast_snapshot(const ReplicationSnapshot &snapshot)
    {
        TELEM_TRACY_ZONE_C("NetManager_broadcast_snapshot", TELEM_COLOR_NET_SEND);

        if (!impl_->initialized || impl_->role != NetRole::server) return;

        TELEM_LOG_DEBUG("NetManager: Broadcasting snapshot (tick {}, {} entities)",
                        snapshot.tick, snapshot.entities.size());

        auto peer_ids = impl_->conn_handler->get_connected_peer_ids();
        for (PeerID id: peer_ids) {
            auto &peer_state = impl_->conn_handler->get_peer_state(id);
            if (!peer_state.is_handshake_complete()) {
                continue;
            }
            NetBuffer buffer;
            snapshot.write(buffer);
            impl_->pipeline->apply_outgoing(impl_->conn_handler->get_peer_state(id), buffer,
                                            SnapshotFlags::FullSnapshot);
            impl_->transport->send(id, buffer,
                                   static_cast<uint8_t>(NetChannel::GameplayUnreliable),
                                   TransportReliability::unreliable_fragmented);
        }
    }

    void NetManager::add_processor(std::unique_ptr<NetProcessor> processor)
    {
        impl_->pipeline->add_processor(std::move(processor));
    }

    void NetManager::set_on_connected(std::function<void(PeerID)> callback)
    {
        impl_->game_on_connected = std::move(callback);
    }

    void NetManager::set_on_disconnected(std::function<void(PeerID, DisconnectReason)> callback)
    {
        impl_->game_on_disconnected = std::move(callback);
    }

    void NetManager::set_on_rejected(std::function<void(PeerID, RejectReason)> callback)
    {
        impl_->game_on_rejected = std::move(callback);
    }

    void NetManager::set_snapshot_callback(SnapshotCallback callback)
    {
        impl_->snapshot_callback = std::move(callback);
    }

    void NetManager::set_transport_for_testing(std::unique_ptr<internal::INetTransport> transport)
    {
        impl_->transport = std::move(transport);
    }
}

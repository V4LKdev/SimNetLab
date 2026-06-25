#include "net_manager.hpp"
#include "net_connection_handler.hpp"
#include "net_transport.hpp"
#include "telemetry/telemetry.hpp"
#include <algorithm>
#include <shared_mutex>
#include <stdexcept>


namespace simnet::core::net {
    using namespace internal;

    struct NetManager::Impl {
        NetRole role = NetRole::client;
        config::SimConfig sim_config;
        std::unique_ptr<INetTransport> transport;
        std::unique_ptr<ConnectionHandler> conn_handler;

        std::function<void(PeerID)> game_on_connected;
        std::function<void(PeerID, DisconnectReason)> game_on_disconnected;
        std::function<void(PeerID, RejectReason)> game_on_rejected;
        std::function<void(const NetworkSnapshot &)> on_snapshot_received;

        // --- delta baseline ring buffer ---
        static constexpr size_t kHistorySize = 32;
        std::array<std::shared_ptr<const NetworkSnapshot>, kHistorySize> history_ring_{};
        std::shared_mutex history_mutex_;

        void insert_snapshot(std::shared_ptr<const NetworkSnapshot> snap)
        {
            if (!snap) return;
            std::unique_lock lock(history_mutex_);
            history_ring_[snap->tick % kHistorySize] = std::move(snap);
        }

        std::shared_ptr<const NetworkSnapshot> get_baseline(uint64_t tick)
        {
            std::shared_lock lock(history_mutex_);
            const auto &s = history_ring_[tick % kHistorySize];
            return (s && s->tick == tick) ? s : nullptr;
        }

        utils::TimePoint current_time;

        bool initialized = false;


        void dispatch_incoming(PeerID id, NetBuffer &buffer) const
        {
            TELEM_TRACY_ZONE_C("NetManager_dispatch_incoming", TELEM_COLOR_NET_RECV);

            if (buffer.size() < 1) {
                TELEM_LOG_WARN("NetManager: empty packet from peer {}", id);
                return;
            }

            const size_t incoming_size = buffer.size();

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

                // TODO: client-side reverse pipeline will go here
                TELEM_COUNTER_INC("net.snapshot_recv", 1);
                TELEM_HISTOGRAM_ADD("net.snapshot_in_size", static_cast<double>(incoming_size));
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

        bool ok = false;
        if (role == NetRole::server) {
            ok = impl_->transport->initialize_server(config.net_port, config.net_max_peers);
        } else {
            ok = impl_->transport->initialize_client();
        }

        if (!ok) {
            TELEM_LOG_ERROR("NetManager: Failed to initialize transport");
            impl_->transport.reset();
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
        return impl_->initialized;
    }

    static std::once_flag enet_deinit_flag;

    void NetManager::shutdown()
    {
        if (!impl_->initialized) return;

        TELEM_LOG_INFO("NetManager: Shutting down");

        impl_->transport->shutdown();
        impl_->conn_handler.reset();
        impl_->transport.reset();
        impl_->initialized = false;

        std::call_once(enet_deinit_flag, [] {
            TELEM_LOG_DEBUG("NetManager: Deinitializing ENet");
            enet_deinitialize();
        });
    }

    void NetManager::update(utils::TimePoint now, int timeout_ms)
    {
        if (!impl_->initialized) return;
        impl_->current_time = now;
        impl_->transport->service(now, timeout_ms);
        impl_->conn_handler->update(now);
    }


    PeerID NetManager::connect(const std::string &address, uint16_t port)
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


    void NetManager::push_snapshot(const NetworkSnapshot &snapshot)
    {
        if (!impl_->initialized || impl_->role != NetRole::server) return;
        impl_->insert_snapshot(std::make_shared<NetworkSnapshot>(snapshot));
    }

    std::shared_ptr<const NetworkSnapshot> NetManager::get_baseline(uint64_t tick)
    {
        if (!impl_->initialized) return nullptr;
        return impl_->get_baseline(tick);
    }

    void NetManager::send_to_peer(PeerID peer, const NetBuffer &buffer,
                                  uint8_t channel, TransportReliability reliability)
    {
        if (!impl_->initialized) return;
        impl_->transport->send(peer, buffer, channel, reliability);
    }

    PeerState &NetManager::get_peer_state(PeerID id) const
    {
        if (!impl_->conn_handler) {
            throw std::runtime_error("NetManager: No connection handler");
        }
        return impl_->conn_handler->get_peer_state(id);
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

    void NetManager::set_on_snapshot_received(std::function<void(const NetworkSnapshot &)> callback)
    {
        impl_->on_snapshot_received = std::move(callback);
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

    void NetManager::set_transport_for_testing(std::unique_ptr<INetTransport> transport)
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

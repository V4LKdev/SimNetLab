#include "net_transport.hpp"
#include "telemetry/telemetry.hpp"

namespace simnet::core::net::internal {
    NetTransport::~NetTransport()
    {
        shutdown();
    }

    bool NetTransport::initialize_server(uint16_t port, size_t max_peers)
    {
        if (host_) shutdown();

        TELEM_LOG_INFO("RealNetTransport: Starting server on port {}, max peers {}", port, max_peers);

        ENetAddress address;
        address.host = ENET_HOST_ANY;
        address.port = port;
        host_ = enet_host_create(&address, max_peers, NUM_NET_CHANNELS, 0, 0);
        if (!host_) {
            TELEM_LOG_ERROR("RealNetTransport: Failed to create server host");
            return false;
        }
        is_server_ = true;
        return true;
    }

    bool NetTransport::initialize_client()
    {
        if (host_) shutdown();

        TELEM_LOG_INFO("RealNetTransport: Starting client, max channels {}", NUM_NET_CHANNELS);

        host_ = enet_host_create(nullptr, 1, NUM_NET_CHANNELS, 0, 0);
        if (!host_) {
            TELEM_LOG_ERROR("RealNetTransport: Failed to create client host");
            return false;
        }
        is_server_ = false;
        return true;
    }

    void NetTransport::shutdown()
    {
        if (!host_ || shutdown_done_) return;

        TELEM_LOG_INFO("RealNetTransport: Shutting down...");

        // ENet will clean up all peers and resources automatically.
        enet_host_destroy(host_);
        host_ = nullptr;
        id_to_peer_.clear();
        peer_ptr_to_id_.clear(); // also clear reverse map
        shutdown_done_ = true;
    }

    PeerID NetTransport::connect(const std::string &address, uint16_t port)
    {
        if (!host_) return 0;

        TELEM_LOG_INFO("RealNetTransport: Connecting to {}:{}", address, port);

        ENetAddress addr;
        if (enet_address_set_host(&addr, address.c_str()) != 0) {
            TELEM_LOG_ERROR("RealNetTransport: Could not resolve address '{}'", address);
            return 0;
        }
        addr.port = port;
        ENetPeer *peer = enet_host_connect(host_, &addr, NUM_NET_CHANNELS, 0);
        if (!peer) {
            TELEM_LOG_ERROR("RealNetTransport: Failed to initiate connection to {}:{}", address, port);
            return 0;
        }
        PeerID id = peer->connectID;
        id_to_peer_[id] = peer;
        peer_ptr_to_id_[peer] = id;
        TELEM_LOG_TRACE("RealNetTransport: Outgoing connection, peer {}", id);
        return id;
    }

    void NetTransport::disconnect(PeerID peer, DisconnectReason reason)
    {
        auto it = id_to_peer_.find(peer);
        if (it == id_to_peer_.end()) {
            TELEM_LOG_WARN("RealNetTransport: disconnect called for unknown peer {}", peer);
            return;
        }

        TELEM_LOG_INFO("RealNetTransport: Disconnecting peer {}, reason {}", peer, static_cast<uint8_t>(reason));

        enet_peer_disconnect(it->second, static_cast<enet_uint32>(reason));
    }


    void NetTransport::send(PeerID peer, const NetBuffer &buffer, uint8_t channel, TransportReliability reliability)
    {
        auto it = id_to_peer_.find(peer);
        if (it == id_to_peer_.end()) {
            TELEM_LOG_WARN("RealNetTransport: send to unknown peer {}", peer);
            return;
        }
        if (buffer.size() == 0) return;

        enet_uint32 flags = 0;
        switch (reliability) {
            case TransportReliability::reliable:
                flags = ENET_PACKET_FLAG_RELIABLE;
                break;
            case TransportReliability::unreliable:
                flags = ENET_PACKET_FLAG_UNSEQUENCED;
                break;
            case TransportReliability::unreliable_fragmented:
                flags = ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
                break;
        }

        TELEM_COUNTER_INC("net.pkt_sent_total", 1);
        TELEM_COUNTER_INC("net.bytes_sent_total", buffer.size());
        TELEM_HISTOGRAM_ADD("net.pkt_size_sent", buffer.size());

        ENetPacket *packet = enet_packet_create(buffer.data(), buffer.size(), flags);
        if (!packet) {
            TELEM_LOG_WARN("RealNetTransport: Failed to create packet for peer {}, size {}", peer, buffer.size());
            return;
        }
        enet_peer_send(it->second, channel, packet);
    }

    void NetTransport::set_callbacks(TransportCallbacks callbacks)
    {
        callbacks_ = std::move(callbacks);
    }

    void NetTransport::service(utils::TimePoint /*now*/, int timeout_ms)
    {
        if (!host_) return;

        ENetEvent event;

        // Wait for at most timeout_ms for a single event
        if (enet_host_service(host_, &event, timeout_ms) > 0) {
            dispatch_event(event);
        }

        // Drain any remaining events without further blocking
        while (enet_host_service(host_, &event, 0) > 0) {
            dispatch_event(event);
        }

        poll_peer_statistics();
    }

    void NetTransport::dispatch_event(ENetEvent &event)
    {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                on_enet_connect(event);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                on_enet_disconnect(event);
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                on_enet_receive(event);
                break;
            default: break;
        }
    }

    void NetTransport::on_enet_connect(const ENetEvent &event)
    {
        PeerID id = event.peer->connectID;
        TELEM_LOG_INFO("RealNetTransport: New connection, peer {}", id);

        id_to_peer_[id] = event.peer;
        peer_ptr_to_id_[event.peer] = id;

        TELEM_COUNTER_INC("net.raw_connections_accepted", 1);

        if (callbacks_.on_new_connection) {
            callbacks_.on_new_connection(id);
        }
    }

    void NetTransport::on_enet_disconnect(const ENetEvent &event)
    {
        ENetPeer *ptr = event.peer;
        enet_uint32 raw_data = event.data;

        // ENet uses 0 as internal timeout/reject
        DisconnectReason reason;
        if (raw_data == 0) {
            reason = DisconnectReason::Timeout;
        } else {
            reason = static_cast<DisconnectReason>(raw_data);
        }

        // Retrieve PeerID from reverse map
        PeerID id = 0;
        auto it = peer_ptr_to_id_.find(ptr);
        if (it != peer_ptr_to_id_.end()) {
            id = it->second;
        } else {
            id = ptr->connectID;
            TELEM_LOG_WARN("RealNetTransport: disconnect for peer not in reverse map, using connectID {}", id);
        }

        TELEM_LOG_INFO("RealNetTransport: Peer {} disconnected, reason {}", id, static_cast<uint8_t>(reason));

        TELEM_COUNTER_INC("net.disconnections_total", 1);

        if (callbacks_.on_disconnection) {
            callbacks_.on_disconnection(id, reason);
        }

        // Cleanup both maps
        id_to_peer_.erase(id);
        peer_ptr_to_id_.erase(ptr);
    }


    void NetTransport::on_enet_receive(const ENetEvent &event)
    {
        PeerID id = event.peer->connectID;
        size_t length = event.packet->dataLength;

        NetBuffer buffer;
        TELEM_LOG_TRACE("RealNetTransport: received packet, peer={}, size={}", id, length);
        buffer.write_raw(event.packet->data, length);
        enet_packet_destroy(event.packet);

        TELEM_COUNTER_INC("net.pkt_recv_total", 1);
        TELEM_COUNTER_INC("net.bytes_recv_total", static_cast<int64_t>(length));
        TELEM_HISTOGRAM_ADD("net.pkt_size_recv", static_cast<double>(length));

        if (callbacks_.on_data)
            callbacks_.on_data(id, buffer);
    }


    void NetTransport::poll_peer_statistics()
    {
        if (!host_) return;

        for (size_t i = 0; i < host_->peerCount; ++i) {
            const ENetPeer *peer = &host_->peers[i];
            if (peer->state != ENET_PEER_STATE_CONNECTED)
                continue;

            // Round-trip time in milliseconds
            TELEM_HISTOGRAM_ADD("net.enet_rtt_ms", static_cast<double>(peer->roundTripTime));

            // Packet loss percentage
            constexpr double scale = static_cast<double>(ENET_PEER_PACKET_LOSS_SCALE);
            double loss_pct = (peer->packetLoss / scale) * 100.0;
            TELEM_HISTOGRAM_ADD("net.enet_packet_loss_pct", loss_pct);

            // Total packets sent/lost reported as gauges (reset to latest value each poll)
            // TELEM_COUNTER_SET("net.enet_packets_sent", static_cast<int64_t>(peer->packetsSent));
            // TELEM_COUNTER_SET("net.enet_packets_lost", static_cast<int64_t>(peer->packetsLost));

            // Log individual peer stats at trace level (very verbose)
            // TELEM_LOG_TRACE("RealNetTransport: stats peer {} rtt={} loss={:.2f}%", peer->connectID, peer->roundTripTime, loss_pct);
        }
    }
}

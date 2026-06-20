#include "connection_handler.hpp"
#include "net_message.hpp"
#include "telemetry.hpp"

namespace simnet::core::net::internal {
    ConnectionHandler::ConnectionHandler(INetTransport &transport,
                                         utils::Milliseconds ping_interval,
                                         utils::Milliseconds timeout)
        : transport_(transport)
          , ping_interval_(ping_interval)
          , timeout_(timeout)
    {
    }

    void ConnectionHandler::update(utils::TimePoint now)
    {
        current_time_ = now;
        send_pings(now);
        check_timeouts(now);
    }

    void ConnectionHandler::on_transport_connect(PeerID id)
    {
        TELEM_LOG_INFO("ConnectionHandler: Peer {} connected", id);

        auto it = peers_.find(id);
        if (it != peers_.end()) {
            // Peer previously registered (e.g., outgoing connection) – update state
            auto &peer = it->second;
            peer.record_activity(current_time_);
            peer.record_connect_time(current_time_);
            peer.mark_handshaking();
            reported_disconnects_.erase(id);

            // If client, send Hello (will be triggered later by set_role logic)
            if (!is_server_) {
                HelloMessage hello(CURRENT_PROTOCOL_VERSION);
                NetBuffer buf;
                hello.serialize(buf);
                send_control(id, buf);
                TELEM_LOG_DEBUG("ConnectionHandler: Sent Hello to peer {}", id);
            }
        } else {
            // New incoming connection
            auto [new_it, inserted] = peers_.emplace(id, PeerState(id));
            auto &peer = new_it->second;
            peer.record_activity(current_time_);
            peer.record_connect_time(current_time_);
            peer.mark_handshaking();
            reported_disconnects_.erase(id);

            if (!is_server_) {
                HelloMessage hello(CURRENT_PROTOCOL_VERSION);
                NetBuffer buf;
                hello.serialize(buf);
                send_control(id, buf);
                TELEM_LOG_DEBUG("ConnectionHandler: Sent Hello to peer {}", id);
            }
        }
    }


    void ConnectionHandler::on_transport_disconnect(PeerID id, DisconnectReason reason)
    {
        TELEM_LOG_INFO("ConnectionHandler: Peer {} disconnected, reason {}", id, static_cast<uint8_t>(reason));

        auto it = peers_.find(id);
        if (it != peers_.end()) {
            it->second.mark_disconnected();
            peers_.erase(it);
        }

        // Fire callback only the first time
        if (on_disconnected && reported_disconnects_.insert(id).second) {
            on_disconnected(id, reason);
        }
    }


    void ConnectionHandler::on_transport_data(PeerID id, NetBuffer &buffer)
    {
        auto it = peers_.find(id);
        if (it == peers_.end()) {
            TELEM_LOG_WARN("ConnectionHandler: Data from unknown peer {}", id);
            return;
        }
        it->second.record_activity(current_time_);

        if (buffer.size() < 1) {
            TELEM_LOG_WARN("ConnectionHandler: Empty packet from peer {}", id);
            return;
        }

        // Peek message type without permanently consuming the byte
        auto msg_type = static_cast<MessageType>(buffer.read<uint8_t>());
        buffer.reset_data();

        if (msg_type == MessageType::Snapshot) {
            // Snapshots are not handled here; they are dispatched by NetManager.
            return;
        }

        auto message = NetMessage::deserialize(buffer);
        if (!message) {
            TELEM_LOG_WARN("ConnectionHandler: Malformed control message type {} from peer {}",
                           static_cast<uint8_t>(msg_type), id);
            return;
        }

        if (auto *hello = dynamic_cast<const HelloMessage *>(message.get())) {
            handle_hello(id, hello->get_version());
        } else if (auto *welcome = dynamic_cast<const WelcomeMessage *>(message.get())) {
            handle_welcome(id);
        } else if (auto *reject = dynamic_cast<const RejectMessage *>(message.get())) {
            handle_reject(id, reject->get_reason());
        } else if (auto *ping = dynamic_cast<const PingMessage *>(message.get())) {
            handle_ping(id);
        }
        // Pong ignored (RTT tracking later)
    }

    void ConnectionHandler::register_outgoing_peer(PeerID id)
    {
        TELEM_LOG_TRACE("ConnectionHandler: register outgoing peer {}", id);

        auto [it, inserted] = peers_.emplace(id, PeerState(id));
        if (!inserted) {
            TELEM_LOG_WARN("ConnectionHandler: peer {} already exists during outgoing register", id);
        }
        auto &peer = it->second;
        peer.record_activity(utils::TimePoint::clock::now());
        peer.mark_connecting();
    }


    // ---- Handshake state machine ----

    void ConnectionHandler::handle_hello(PeerID id, ProtocolVersion version)
    {
        TELEM_LOG_DEBUG("ConnectionHandler: Received Hello from peer {}, version {}", id, version);

        auto it = peers_.find(id);
        if (it == peers_.end()) return;

        if (version == CURRENT_PROTOCOL_VERSION) {
            WelcomeMessage welcome;
            NetBuffer buf;
            welcome.serialize(buf);
            send_control(id, buf);

            it->second.mark_connected();

            double dur_ms = std::chrono::duration<double, std::milli>(
                current_time_ - it->second.connect_time()).count();
            TELEM_HISTOGRAM_ADD("net.handshake_duration_ms", dur_ms);
            TELEM_COUNTER_INC("net.handshakes_completed", 1);

            it->second.record_activity(current_time_);
            TELEM_LOG_DEBUG("ConnectionHandler: Sent Welcome to peer {}", id);
            if (on_connected) on_connected(id);
        } else {
            TELEM_LOG_WARN("ConnectionHandler: Version mismatch for peer {} ({} != {})",
                           id, version, CURRENT_PROTOCOL_VERSION);
            RejectMessage reject(RejectReason::VersionMismatch);
            NetBuffer buf;
            reject.serialize(buf);
            send_control(id, buf);
            transport_.disconnect(id, DisconnectReason::Rejected);
        }
    }

    void ConnectionHandler::handle_welcome(PeerID id)
    {
        TELEM_LOG_DEBUG("ConnectionHandler: Received Welcome from peer {}", id);

        auto it = peers_.find(id);
        if (it == peers_.end()) return;

        it->second.mark_connected();

        double dur_ms = std::chrono::duration<double, std::milli>(
            current_time_ - it->second.connect_time()).count();
        TELEM_HISTOGRAM_ADD("net.handshake_duration_ms", dur_ms);
        TELEM_COUNTER_INC("net.handshakes_completed", 1);

        it->second.record_activity(current_time_);
        if (on_connected) on_connected(id);
    }

    void ConnectionHandler::handle_reject(PeerID id, RejectReason reason)
    {
        TELEM_LOG_WARN("ConnectionHandler: Received Reject from peer {}, reason {}", id, static_cast<uint8_t>(reason));

        if (on_rejected) on_rejected(id, reason);
        // The connection will be terminated by the transport.
    }

    void ConnectionHandler::handle_ping(PeerID id)
    {
        TELEM_LOG_TRACE("ConnectionHandler: Received Ping from peer {}", id);
        TELEM_COUNTER_INC("net.ping_received", 1);

        PongMessage pong;
        NetBuffer buf;
        pong.serialize(buf);
        send_control(id, buf);
    }

    // ---- Periodic tasks ----

    void ConnectionHandler::check_timeouts(utils::TimePoint now)
    {
        std::vector<PeerID> timed_out;

        for (auto &[id, peer]: peers_) {
            auto state = peer.get_state();
            if (state == ConnectionState::connecting ||
                state == ConnectionState::handshaking ||
                state == ConnectionState::connected) {
                if (now - peer.last_activity_time() > timeout_) {
                    TELEM_LOG_WARN("ConnectionHandler: Peer {} timed out after {} ms",
                                   id, utils::to_milliseconds_saturating(now - peer.last_activity_time()));
                    TELEM_COUNTER_INC("net.timeout_detected", 1);
                    timed_out.push_back(id);
                }
            }
        }

        for (PeerID id: timed_out) {
            auto it = peers_.find(id);
            if (it == peers_.end()) continue;

            auto &peer = it->second;
            auto state = peer.get_state();

            if (state == ConnectionState::connecting) {
                // Connection never established – fire callback immediately, clean up.
                // transport_ disconnect is safe even for unconnected peers.
                transport_.disconnect(id, DisconnectReason::Timeout);
                // The disconnect event will arrive later; we already avoid duplicate callback
                // by calling on_transport_disconnect now, which marks it as reported.
                on_transport_disconnect(id, DisconnectReason::Timeout);
            } else {
                transport_.disconnect(id, DisconnectReason::Timeout);
                peer.mark_disconnecting();
            }
        }
    }


    void ConnectionHandler::send_pings(utils::TimePoint now)
    {
        for (auto &[id, peer]: peers_) {
            if (peer.is_handshake_complete()) {
                if (now - peer.last_ping_sent_time() > ping_interval_) {
                    PingMessage ping;
                    NetBuffer buf;
                    ping.serialize(buf);
                    send_control(id, buf);
                    peer.record_ping_sent(now);
                    TELEM_LOG_TRACE("ConnectionHandler: Sending ping to peer {}", id);
                    TELEM_COUNTER_INC("net.ping_sent", 1);
                }
            }
        }
    }

    void ConnectionHandler::send_control(PeerID id, const NetBuffer &buffer)
    {
        transport_.send(id, buffer, static_cast<uint8_t>(NetChannel::SystemReliable),
                        TransportReliability::reliable);
    }

    std::vector<PeerID> ConnectionHandler::get_connected_peer_ids() const
    {
        std::vector<PeerID> result;
        for (const auto &[id, peer]: peers_) {
            if (peer.is_handshake_complete()) {
                result.push_back(id);
            }
        }
        return result;
    }

    PeerState &ConnectionHandler::get_peer_state(PeerID id)
    {
        return peers_.at(id);
    }

    void ConnectionHandler::record_peer_activity(PeerID id, utils::TimePoint now)
    {
        auto it = peers_.find(id);
        if (it != peers_.end()) {
            it->second.record_activity(now);
        }
    }

    PeerState *ConnectionHandler::find_peer(PeerID id)
    {
        const auto it = peers_.find(id);
        return (it != peers_.end()) ? &it->second : nullptr;
    }
}

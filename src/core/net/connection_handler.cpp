#include "connection_handler.hpp"
#include "net_message.hpp"
#include "telemetry/telemetry.hpp"

namespace simnet::core::net::internal {
    ConnectionHandler::ConnectionHandler(INetTransport &transport)
        : transport_(transport)
    {
    }

    void ConnectionHandler::update(utils::TimePoint now)
    {
        current_time_ = now;
    }

    void ConnectionHandler::on_transport_connect(PeerID id)
    {
        TELEM_LOG_INFO("ConnectionHandler: Peer {} connected", id);

        auto it = peers_.find(id);
        if (it == peers_.end()) {
            auto [new_it, _] = peers_.emplace(id, PeerState(id));
            it = new_it;
        }

        auto &peer = it->second;
        peer.record_connect_time(current_time_);
        peer.mark_handshaking();
        reported_disconnects_.erase(id);

        // Only send Hello if we are the client
        if (!is_server_) {
            HelloMessage hello(CURRENT_PROTOCOL_VERSION);
            NetBuffer buf;
            hello.serialize(buf);
            send_control(id, buf);
            TELEM_LOG_DEBUG("ConnectionHandler: Sent Hello to peer {}", id);
        }
    }

    void ConnectionHandler::on_transport_disconnect(PeerID id, DisconnectReason reason)
    {
        TELEM_LOG_INFO("ConnectionHandler: Peer {} disconnected, reason {}",
                       id, static_cast<uint8_t>(reason));

        auto it = peers_.find(id);
        if (it != peers_.end()) {
            it->second.mark_disconnected();
            peers_.erase(it);
        }

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

        if (buffer.size() < 1) {
            TELEM_LOG_WARN("ConnectionHandler: Empty packet from peer {}", id);
            return;
        }

        auto msg_type = static_cast<MessageType>(buffer.read<uint8_t>());
        buffer.reset_data();

        // Snapshots are handled by NetManager, not here.
        if (msg_type == MessageType::Snapshot) return;

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
        }
        // Other types (e.g., future game events) ignored here.
    }

    void ConnectionHandler::register_outgoing_peer(PeerID id)
    {
        TELEM_LOG_TRACE("ConnectionHandler: register outgoing peer {}", id);

        auto [it, inserted] = peers_.emplace(id, PeerState(id));
        if (!inserted) {
            TELEM_LOG_WARN("ConnectionHandler: peer {} already exists during outgoing register", id);
        }
        it->second.mark_connecting();
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

        if (on_connected) on_connected(id);
    }

    void ConnectionHandler::handle_reject(PeerID id, RejectReason reason)
    {
        TELEM_LOG_WARN("ConnectionHandler: Received Reject from peer {}, reason {}",
                       id, static_cast<uint8_t>(reason));
        if (on_rejected) on_rejected(id, reason);
    }

    void ConnectionHandler::send_control(PeerID id, const NetBuffer &buffer)
    {
        transport_.send(id, buffer,
                        static_cast<uint8_t>(NetChannel::System),
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

    PeerState *ConnectionHandler::find_peer(PeerID id)
    {
        const auto it = peers_.find(id);
        return (it != peers_.end()) ? &it->second : nullptr;
    }
}

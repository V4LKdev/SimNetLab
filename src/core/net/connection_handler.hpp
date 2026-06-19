#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "net_transport_interface.hpp"
#include "peer_state.hpp"
#include "utils/time_keeper.hpp"

namespace simnet::core::net::internal {
    class ConnectionHandler {
    public:
        using on_connected_t = std::function<void(PeerID)>;
        using on_disconnected_t = std::function<void(PeerID, DisconnectReason)>;
        using on_rejected_t = std::function<void(PeerID, RejectReason)>;

        ConnectionHandler(INetTransport &transport,
                          utils::Milliseconds ping_interval,
                          utils::Milliseconds timeout);

        on_connected_t on_connected;
        on_disconnected_t on_disconnected;
        on_rejected_t on_rejected;

        void set_role(bool is_server) { is_server_ = is_server; }

        void update(utils::TimePoint now);

        void on_transport_connect(PeerID id);

        void on_transport_disconnect(PeerID id, DisconnectReason reason);

        void on_transport_data(PeerID id, NetBuffer &buffer);

        // Register a peer before it is connected (outgoing attempt)
        void register_outgoing_peer(PeerID id);

        std::vector<PeerID> get_connected_peer_ids() const;

        PeerState &get_peer_state(PeerID id);

        void record_peer_activity(PeerID id, utils::TimePoint now)
        {
            auto it = peers_.find(id);
            if (it != peers_.end()) {
                it->second.record_activity(now);
            }
        }

        PeerState *find_peer(PeerID id);

    private:
        INetTransport &transport_;
        bool is_server_ = false;

        utils::Milliseconds ping_interval_;
        utils::Milliseconds timeout_;
        utils::TimePoint current_time_{};

        std::unordered_map<PeerID, PeerState> peers_;
        std::unordered_set<PeerID> reported_disconnects_;

        void send_control(PeerID id, const NetBuffer &buffer);

        void handle_hello(PeerID id, ProtocolVersion version);

        void handle_welcome(PeerID id);

        void handle_reject(PeerID id, RejectReason reason);

        void handle_ping(PeerID id);

        void check_timeouts(utils::TimePoint now);

        void send_pings(utils::TimePoint now);
    };
}

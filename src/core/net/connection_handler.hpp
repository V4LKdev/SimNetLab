#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "net_transport_interface.hpp"
#include "peer_state.hpp"
#include "core/utils/time_keeper.hpp"

namespace simnet::core::net::internal {
    /**
     * @brief Manages peer connection lifecycle and handshake protocol.
     *
     * Owns the per‑peer state map and routes incoming control messages
     * (Hello/Welcome/Reject) to the appropriate handler.  Snapshot
     * packets are ignored – they are dispatched directly by NetManager.
     *
     * Timeouts, keep‑alive, and RTT measurement are handled by ENet;
     * this class only tracks application‑level handshake completion.
     */
    class ConnectionHandler {
    public:
        using on_connected_t = std::function<void(PeerID)>;
        using on_disconnected_t = std::function<void(PeerID, DisconnectReason)>;
        using on_rejected_t = std::function<void(PeerID, RejectReason)>;

        explicit ConnectionHandler(INetTransport &transport, const config::SimConfig &config);

        on_connected_t on_connected;
        on_disconnected_t on_disconnected;
        on_rejected_t on_rejected;

        void set_role(bool is_server) { is_server_ = is_server; }

        void set_transport(INetTransport &transport) { transport_ = &transport; }

        /** Called each tick by NetManager (reserved for future use). */
        void update(utils::TimePoint now);

        void on_transport_connect(PeerID id);

        void on_transport_disconnect(PeerID id, DisconnectReason reason);

        void on_transport_data(PeerID id, NetBuffer &buffer);

        void register_outgoing_peer(PeerID id);

        std::vector<PeerID> get_connected_peer_ids() const;

        PeerState &get_peer_state(PeerID id);

        PeerState *find_peer(PeerID id);

    private:
        INetTransport *transport_ = nullptr;
        bool is_server_ = false;
        const config::SimConfig &config_;

        utils::TimePoint current_time_{};

        std::unordered_map<PeerID, PeerState> peers_;
        std::unordered_set<PeerID> reported_disconnects_;

        void send_control(PeerID id, const NetBuffer &buffer);

        void handle_hello(PeerID id, ProtocolVersion version, uint64_t client_fingerprint);

        void handle_welcome(PeerID id);

        void handle_reject(PeerID id, RejectReason reason);
    };
}

#pragma once
#include <chrono>
#include <unordered_map>
#include <vector>
#include <enet/enet.h>

#include "protocol.hpp"

namespace simnet::server {
    enum class ClientState : uint8_t {
        Handshaking,
        Connected,
        Disconnecting
    };

    struct ClientInfo {
        ENetPeer *peer = nullptr;
        ClientState state = ClientState::Handshaking;
        std::chrono::steady_clock::time_point connect_time;
        std::chrono::steady_clock::time_point last_packet_time;
    };

    class network_server {
    public:
        explicit network_server(enet_uint16 port, int max_clients = 10);

        ~network_server();

        network_server(const network_server &) = delete;

        network_server &operator=(const network_server &) = delete;

        network_server(network_server &&) = delete;

        network_server &operator=(network_server &&) = delete;


        /// Non-blocking event pump.
        void service();

        /// Returns true while the host is alive
        [[nodiscard]]
        bool is_running() const noexcept;

        /// Number of currently connected peers
        [[nodiscard]]
        int client_count() const noexcept;

        /// Returns the underlying ENet host (for manual event pump)
        [[nodiscard]]
        ENetHost *get_host() const noexcept { return server_host_; }

        /// Handle a single network event
        void process_event(const ENetEvent &event);

        /// Check and disconnect timed-out / handshake-expired clients
        void check_timeouts();

    private:
        void handle_packet(ENetPeer *peer, const ENetPacket *packet);

        void send_welcome(ENetPeer *peer);

        void send_reject(ENetPeer *peer, net::RejectReason reason);

        void send_ping(ENetPeer *peer);

        void send_pong(ENetPeer *peer);


        void reset_peer(ENetPeer *peer);

        /// Disconnect all peers gracefully, then force if necessary
        void disconnect_all();

        void disconnect(ENetPeer *peer, net::DisconnectReason reason);

        ENetHost *server_host_ = nullptr;
        std::unordered_map<ENetPeer *, ClientInfo> clients_;
        ENetAddress server_address_{};
        enet_uint16 port_;
        int max_clients_;

        std::chrono::milliseconds handshake_timeout_{5000};
        std::chrono::milliseconds service_timeout_{10000};
        std::chrono::milliseconds disconnect_timeout_{3000};
    };
}

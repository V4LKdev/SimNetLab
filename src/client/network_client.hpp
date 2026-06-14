#pragma once
#include <chrono>
#include <enet/enet.h>

namespace simnet::client {
    enum class ConnectionState : uint8_t {
        Disconnected = 0,
        Connecting = 1,
        Handshaking = 2,
        Connected = 3
    };

    /**
     *  ENet client wrapper that handles connection lifecycle and non-blocking event polling.
     */
    class network_client {
    public:
        network_client();

        ~network_client();

        network_client(const network_client &) = delete;

        network_client &operator=(const network_client &) = delete;

        network_client(network_client &&) = delete;

        network_client &operator=(network_client &&) = delete;

        /// Non-blocking connection initiation.
        void start_connect(const char *host, enet_uint16 port);

        /// Attempt graceful disconnect from the remote host, else forced. BLOCKING
        void try_graceful_disconnect();

        /// Non-blocking event pump.
        void service();

        /// True if a connection to a remote peer is currently established.
        [[nodiscard]]
        bool is_connected() const noexcept;

        [[nodiscard]]
        ConnectionState state() const noexcept { return state_; }

    private:
        void handle_packet(ENetPacket *packet);

        void send_hello();

        void send_ping();

        void send_pong();

        void reset_peer();

        /// Underlying enet client host
        ENetHost *client_host_ = nullptr;
        /// Remote peer after successful connect (Server)
        ENetPeer *peer_ = nullptr;
        ConnectionState state_ = ConnectionState::Disconnected;

        std::chrono::milliseconds connect_timeout_ms_{5000};
        std::chrono::milliseconds handshake_timeout_ms_{3000};
        std::chrono::milliseconds service_timeout_ms_{5000};
        std::chrono::milliseconds disconnect_timeout_ms_{3000};
        std::chrono::milliseconds ping_interval_ms_{1000};

        std::chrono::steady_clock::time_point connect_start_time_;
        std::chrono::steady_clock::time_point handshake_start_time_;
        std::chrono::steady_clock::time_point last_packet_time_;
        std::chrono::steady_clock::time_point last_ping_sent_time_;
    };
}

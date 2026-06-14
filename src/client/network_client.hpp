#pragma once
#include <chrono>
#include <enet/enet.h>

namespace simnet::client {
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

        /// Initiate a connection to an ENet server. BLOCKING
        bool connect(const char *address_str, enet_uint16 port);

        /// Attempt graceful disconnect from the remote host, else forced. BLOCKING
        void disconnect();

        /// Non-blocking event pump.
        void service();

        /// True if a connection to a remote peer is currently established.
        [[nodiscard]]
        bool is_connected() const;

    private:
        /// Underlying enet client host
        ENetHost *client_host_ = nullptr;
        /// Remote peer after successful connect (Server)
        ENetPeer *peer_ = nullptr;
        bool connected_ = false;

        enet_uint32 connect_timeout_ms_ = 5000;
        enet_uint32 service_timeout_ms_ = 5000;
        enet_uint32 disconnect_timeout_ms_ = 3000;

        std::chrono::steady_clock::time_point last_packet_time_;
    };
}

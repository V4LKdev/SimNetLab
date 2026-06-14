#include "network_client.hpp"

#include "telemetry.hpp"

namespace simnet::client {
    network_client::network_client()
    {
        if (enet_initialize() != 0) {
            TELEM_LOG_ERROR("Fatal: An error occurred while initializing the ENet.");
            std::abort();
        }

        client_host_ = enet_host_create(
            nullptr, /* Creates a client host */
            1, /* Only allow 1 outgoing connection */
            2, /* Allow up 2 channels to be used, 0 and 1 */
            0, /* Assume any amount of incoming bandwidth */
            0 /* Assume any amount of outgoing bandwidth */
        );

        if (client_host_ == nullptr) {
            TELEM_LOG_ERROR("Fatal: An error occurred while creating the ENet client host.");
            std::abort();
        }

        TELEM_LOG_DEBUG("ENet local host created");
    }

    network_client::~network_client()
    {
        if (client_host_ != nullptr) {
            if (is_connected()) {
                // Forceful immediate disconnect
                enet_peer_reset(peer_);
            }
            enet_host_destroy(client_host_);
            client_host_ = nullptr;
        }

        enet_deinitialize();
    }

    void network_client::disconnect()
    {
        TELEM_LOG_DEBUG("Attempting to disconnect");

        if (!client_host_ || !peer_) { return; }

        enet_peer_disconnect(peer_, 0);

        ENetEvent event;
        while (enet_host_service(client_host_, &event, disconnect_timeout_ms_) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_RECEIVE:
                    enet_packet_destroy(event.packet);
                    break;

                case ENET_EVENT_TYPE_DISCONNECT:
                    TELEM_LOG_INFO("Enet client disconnected gracefully.");
                    peer_ = nullptr;
                    connected_ = false;
                    return;

                default:
                    break;
            }
        }

        // Forced reset
        TELEM_LOG_INFO("Enet client disconnected forcefully.");
        peer_ = nullptr;
        connected_ = false;
    }

    bool network_client::connect(const char *address_str, enet_uint16 port)
    {
        TELEM_LOG_DEBUG("Connecting to {}:{}", address_str, port);

        if (client_host_ == nullptr) { return false; }

        ENetAddress addr;
        if (enet_address_set_host(&addr, address_str) != 0) {
            TELEM_LOG_ERROR("Failed to resolve host: {}", address_str);
            return false;
        }
        addr.port = port;

        peer_ = enet_host_connect(client_host_, &addr, 2, 0);
        if (peer_ == nullptr) {
            TELEM_LOG_ERROR("No available peers to initiate connection: {}:{}", address_str, port);
            return false;
        }

        ENetEvent event;

        while (enet_host_service(client_host_, &event, connect_timeout_ms_) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                connected_ = true;
                TELEM_LOG_INFO("Enet client connected to {}:{}", address_str, port);
                return true;
            }
            if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                TELEM_LOG_WARN("Connection attempt rejected by server");
                peer_ = nullptr;
                return false;
            }
        }

        TELEM_LOG_WARN("Connection attempt timed out");
        enet_peer_reset(peer_);
        peer_ = nullptr;
        TELEM_LOG_DEBUG("Peer reset after failed connection attempt");
        return false;
    }

    void network_client::service()
    {
        TELEM_TRACY_ZONE_C("NetClientService", TELEM_COLOR_NET_RECV);
        if (client_host_ == nullptr) { return; }

        ENetEvent event;
        bool received_event = false;

        // Serve is polling, without timeout, so to be non-blocking
        while (enet_host_service(client_host_, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    TELEM_LOG_WARN("Unexpected connection attempt during serve received");
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT: {
                    TELEM_LOG_INFO("Disconnected by server");
                    connected_ = false;
                    peer_ = nullptr;
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE: {
                    TELEM_LOG_TRACE("Received packet ({} bytes)", event.packet->dataLength);
                    // Handler here
                    enet_packet_destroy(event.packet);
                    last_packet_time_ = std::chrono::steady_clock::now();
                    received_event = true;
                    break;
                }
                default:
                    break;
            }
        }

        // Silent‑connection watchdog (only when connected)
        if (connected_) {
            const auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_packet_time_);

            if (elapsed.count() > static_cast<int64_t>(service_timeout_ms_)) {
                TELEM_LOG_WARN("No packets received from peer in {} ms", service_timeout_ms_);
                // reset timer to avoid log spam
                last_packet_time_ = now;
            }
        }
    }

    bool network_client::is_connected() const
    {
        return connected_ && peer_ != nullptr;
    }
}

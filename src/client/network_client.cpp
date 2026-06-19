#include "network_client.hpp"

#include "telemetry.hpp"
#include "../core/net/old/packet_utils.hpp"

namespace simnet::client {
    network_client::network_client()
        : last_packet_time_(std::chrono::steady_clock::now())
    {
        if (enet_initialize() != 0) {
            TELEM_LOG_ERROR("Fatal: An error occurred while initializing ENet.");
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
            enet_deinitialize();
            std::abort();
        }

        TELEM_LOG_DEBUG("ENet local host created");
    }

    network_client::~network_client()
    {
        if (client_host_ != nullptr) {
            try_graceful_disconnect();
            enet_host_destroy(client_host_);
            client_host_ = nullptr;
        }

        enet_deinitialize();
    }

    void network_client::start_connect(const char *host, enet_uint16 port)
    {
        if (client_host_ == nullptr) return;

        if (state_ != ConnectionState::Disconnected) {
            TELEM_LOG_WARN("start_connect called while not disconnected; state={}",
                           static_cast<int>(state_));
            return;
        }

        ENetAddress addr;
        if (enet_address_set_host(&addr, host) != 0) {
            TELEM_LOG_ERROR("Failed to resolve host: {}", host);
            return;
        }
        addr.port = port;

        peer_ = enet_host_connect(client_host_, &addr, 2, 0);
        if (peer_ == nullptr) {
            TELEM_LOG_ERROR("No available peers for connection to {}:{}", host, port);
            return;
        }

        state_ = ConnectionState::Connecting;
        connect_start_time_ = std::chrono::steady_clock::now();
        TELEM_LOG_INFO("Initiating connection to {}:{}", host, port);
    }

    void network_client::try_graceful_disconnect()
    {
        TELEM_LOG_DEBUG("Attempting to disconnect");

        if (!client_host_ || !peer_) { return; }

        enet_peer_disconnect(peer_, static_cast<enet_uint32>(net::DisconnectReason::ClientQuit));

        ENetEvent event;
        while (enet_host_service(client_host_, &event, disconnect_timeout_ms_.count()) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_RECEIVE:
                    enet_packet_destroy(event.packet);
                    break;

                case ENET_EVENT_TYPE_DISCONNECT:
                    TELEM_LOG_INFO("Enet client disconnected gracefully.");
                    reset_peer();
                    return;

                default:
                    break;
            }
        }

        // Forced reset
        TELEM_LOG_INFO("Enet client disconnected forcefully.");
        reset_peer();
    }

    void network_client::service()
    {
        TELEM_TRACY_ZONE_C("NetClientService", TELEM_COLOR_NET_RECV);
        if (client_host_ == nullptr) { return; }

        ENetEvent event;

        // Serve is polling, without timeout, so to be non-blocking
        while (enet_host_service(client_host_, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    if (state_ == ConnectionState::Connecting) {
                        state_ = ConnectionState::Handshaking;
                        handshake_start_time_ = std::chrono::steady_clock::now();
                        send_hello();

                        TELEM_LOG_DEBUG("Initiated handshake");
                    } else {
                        TELEM_LOG_WARN("Unexpected connection attempt during serve received");
                    }
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT: {
                    TELEM_LOG_INFO("Disconnected by server with reason: {}", event.data);
                    reset_peer();
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE: {
                    TELEM_LOG_TRACE("Received packet ({} bytes)", event.packet->dataLength);

                    handle_packet(event.packet);

                    enet_packet_destroy(event.packet);
                    break;
                }
                default:
                    break;
            }
        }

        const auto now = std::chrono::steady_clock::now();

        switch (state_) {
            case ConnectionState::Connecting: {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - connect_start_time_);
                if (elapsed > connect_timeout_ms_) {
                    TELEM_LOG_WARN("Connection attempt timed out");
                    reset_peer();
                }
                break;
            }
            case ConnectionState::Handshaking: {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - handshake_start_time_);
                if (elapsed > handshake_timeout_ms_) {
                    TELEM_LOG_WARN("Handshake timed out");
                    reset_peer();
                }
                break;
            }
            case ConnectionState::Connected: {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_packet_time_);
                if (elapsed > service_timeout_ms_) {
                    TELEM_LOG_WARN("No packets received from peer in {} ms", service_timeout_ms_.count());
                    // reset timer to avoid log spam
                    last_packet_time_ = now;
                }

                // Send heartbeat
                auto ping_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ping_sent_time_);
                if (ping_elapsed > ping_interval_ms_) {
                    send_ping();
                    last_ping_sent_time_ = now;
                }

                break;
            }
            default:
                break;
        }
    }

    bool network_client::is_connected() const noexcept
    {
        return state_ == ConnectionState::Connected && peer_ != nullptr;
    }

    void network_client::handle_packet(ENetPacket *packet)
    {
        net::MessageType msg = net::read_message_type(packet);

        switch (msg) {
            case net::MessageType::Welcome: {
                if (state_ == ConnectionState::Handshaking) {
                    state_ = ConnectionState::Connected;

                    TELEM_LOG_INFO("Handshake complete");
                    last_packet_time_ = std::chrono::steady_clock::now();
                }
                break;
            }
            case net::MessageType::Reject: {
                if (state_ == ConnectionState::Handshaking) {
                    auto reason = net::read_reject_reason(packet);
                    TELEM_LOG_WARN("Handshake rejected (reason={})",
                                   static_cast<int>(reason));
                    reset_peer();
                    return;
                }
                break;
            }
            case net::MessageType::Ping: {
                last_packet_time_ = std::chrono::steady_clock::now();
                send_pong();
                break;
            }
            case net::MessageType::Pong: {
                auto now = std::chrono::steady_clock::now();

                const std::chrono::milliseconds rtt = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_ping_sent_time_);
                TELEM_LOG_DEBUG("RTT: {}", rtt.count());

                last_packet_time_ = now;
                break;
            }
            default:
                break;
        }
    }

    void network_client::send_hello()
    {
        ENetPacket *pkt = net::create_hello(net::CURRENT_PROTOCOL_VERSION);

        enet_peer_send(peer_, 0, pkt);

        TELEM_LOG_DEBUG("Sending hello");
    }

    void network_client::send_ping()
    {
        ENetPacket *pkt = net::create_ping();
        enet_peer_send(peer_, 1, pkt);
    }

    void network_client::send_pong()
    {
        ENetPacket *pkt = net::create_pong();
        enet_peer_send(peer_, 1, pkt);
    }

    void network_client::reset_peer()
    {
        if (peer_) {
            enet_peer_reset(peer_);
            peer_ = nullptr;
        }
        state_ = ConnectionState::Disconnected;
    }
}

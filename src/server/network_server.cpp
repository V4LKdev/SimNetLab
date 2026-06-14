
#include "network_server.hpp"

#include <algorithm>
#include <chrono>

#include "packet_utils.hpp"
#include "telemetry.hpp"

namespace simnet::server {
    network_server::network_server(const enet_uint16 port, const int max_clients)
        : server_address_({}),
          port_(port),
          max_clients_(max_clients)
    {
        if (enet_initialize() != 0) {
            TELEM_LOG_ERROR("Fatal: An error occurred while initializing ENet.");
            std::abort();
        }

        /* Bind the server to the default localhost.     */
        /* A specific host address can be specified by   */
        /* enet_address_set_host (& address, "x.x.x.x"); */
        server_address_.host = ENET_HOST_ANY;
        server_address_.port = port_;

        server_host_ = enet_host_create(
            &server_address_,
            static_cast<size_t>(max_clients_),
            2, /* allow up to 2 channels to be used, 0 and 1 */
            0, /* no incoming bandwidth limit */
            0 /* no outgoing bandwidth limit */
        );

        if (server_host_ == nullptr) {
            TELEM_LOG_ERROR("Fatal: An error occurred while trying to create a server host.");
            enet_deinitialize();
            std::abort();
        }

        TELEM_LOG_DEBUG("Server initialized and listening on port {}", port_);
    }

    network_server::~network_server()
    {
        if (server_host_) {
            disconnect_all();
            enet_host_destroy(server_host_);
            server_host_ = nullptr;
        }
        enet_deinitialize();
        TELEM_LOG_INFO("Server shut down");
    }

    void network_server::disconnect_all()
    {
        // Start graceful disconnect for every peer.
        for (auto &[peer, info]: clients_) {
            enet_peer_disconnect(peer, static_cast<enet_uint32>(net::DisconnectReason::ServerClosed));
        }

        const auto deadline = std::chrono::steady_clock::now() + disconnect_timeout_;

        while (!clients_.empty()) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            enet_uint32 wait_ms = static_cast<enet_uint32>(std::min<decltype(remaining)>(remaining, 100));

            ENetEvent event;
            if (enet_host_service(server_host_, &event, wait_ms) > 0) {
                if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                    clients_.erase(event.peer);
                    TELEM_LOG_DEBUG("Peer disconnected during server shutdown");
                } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                    enet_packet_destroy(event.packet);
                }
            }
        }

        // Force‑reset any leftover peers.
        if (!clients_.empty()) {
            TELEM_LOG_WARN("Forcing disconnection of {} client(s)", clients_.size());
            for (auto &[peer, info]: clients_) {
                enet_peer_reset(peer);
            }
            clients_.clear();
        }
    }

    void network_server::disconnect(ENetPeer *peer, net::DisconnectReason reason)
    {
        enet_peer_disconnect(peer, static_cast<enet_uint32>(reason));
    }


    void network_server::service()
    {
        TELEM_TRACY_ZONE_C("NetServerService", TELEM_COLOR_NET_RECV);
        if (!server_host_) return;

        ENetEvent event;
        // drain all pending events non‑blocking
        while (enet_host_service(server_host_, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    TELEM_LOG_INFO("Client connected from {}:{} (handshaking)",
                                   event.peer->address.host, event.peer->address.port);

                    auto now = std::chrono::steady_clock::now();
                    clients_.emplace(event.peer, ClientInfo{event.peer, ClientState::Handshaking, now, now});
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT: {
                    auto reason = static_cast<net::DisconnectReason>(event.data);
                    TELEM_LOG_INFO("Client disconnected (reason={})", static_cast<int>(reason));

                    clients_.erase(event.peer);
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE: {
                    TELEM_LOG_TRACE("Received packet ({} bytes)", event.packet->dataLength);

                    auto it = clients_.find(event.peer);
                    if (it != clients_.end()) {
                        handle_packet(event.peer, event.packet);
                        enet_packet_destroy(event.packet);
                    } else {
                        TELEM_LOG_WARN("Received packet from unknown peer - dropping");
                        enet_packet_destroy(event.packet);
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // run timeout checks after event processing
        check_timeouts();
    }

    bool network_server::is_running() const noexcept
    {
        return server_host_ != nullptr;
    }

    int network_server::client_count() const noexcept
    {
        return static_cast<int>(clients_.size());
    }

    void network_server::handle_packet(ENetPeer *peer, ENetPacket *packet)
    {
        const net::MessageType msg = net::read_message_type(packet);
        auto it = clients_.find(peer);
        if (it == clients_.end()) { return; } // safety

        ClientInfo &info = it->second;
        info.last_packet_time = std::chrono::steady_clock::now();

        switch (msg) {
            case net::MessageType::Hello: {
                if (info.state == ClientState::Handshaking) {
                    net::ProtocolVersion ver = net::read_hello_version(packet);
                    if (ver == net::CURRENT_PROTOCOL_VERSION) {
                        send_welcome(peer);
                        info.state = ClientState::Connected;
                        TELEM_LOG_INFO("Client handshake complete");
                    } else {
                        TELEM_LOG_INFO("Client version mismatch: expected {}, got {}",
                                       net::CURRENT_PROTOCOL_VERSION, ver);
                        send_reject(peer, net::RejectReason::VersionMismatch);
                        disconnect(peer, net::DisconnectReason::Other);
                    }
                } else {
                    TELEM_LOG_WARN("Ignoring Hello from already-connected client");
                }
                break;
            }
            case net::MessageType::Ping: {
                send_pong(peer);
                break;
            }
            case net::MessageType::Pong: {
                break;
            }
            default:
                TELEM_LOG_DEBUG("Ignoring unhandled message type {}", static_cast<int>(msg));
                break;
        }
    }

    void network_server::send_welcome(ENetPeer *peer)
    {
        ENetPacket *pkt = net::create_welcome();
        if (!pkt) { return; }

        enet_peer_send(peer, 0, pkt);
    }

    void network_server::send_reject(ENetPeer *peer, net::RejectReason reason)
    {
        ENetPacket *pkt = net::create_reject(reason);
        if (!pkt) { return; }

        enet_peer_send(peer, 0, pkt);
    }

    void network_server::send_ping(ENetPeer *peer)
    {
        ENetPacket *pkt = net::create_ping();
        if (!pkt) { return; }
        enet_peer_send(peer, 1, pkt);
    }

    void network_server::send_pong(ENetPeer *peer)
    {
        ENetPacket *pkt = net::create_pong();
        if (!pkt) { return; }
        enet_peer_send(peer, 1, pkt);
    }

    void network_server::reset_peer(ENetPeer *peer)
    {
        if (!peer) { return; }
        enet_peer_reset(peer);
        clients_.erase(peer);
    }

    void network_server::check_timeouts()
    {
        const auto now = std::chrono::steady_clock::now();

        std::vector<ENetPeer *> to_reset;

        for (auto &[peer, info]: clients_) {
            if (info.state == ClientState::Handshaking) {
                if (now - info.connect_time > handshake_timeout_) {
                    TELEM_LOG_WARN("Handshake timeout - client {}:{}", peer->address.host, peer->address.port);
                    to_reset.push_back(peer);
                }
            } else if (info.state == ClientState::Connected) {
                if (now - info.last_packet_time > service_timeout_) {
                    TELEM_LOG_WARN("Heartbeat timeout - client {}:{}", peer->address.host, peer->address.port);
                    to_reset.push_back(peer);
                }
            }
        }
        for (ENetPeer *peer: to_reset) {
            disconnect(peer, net::DisconnectReason::Timeout);
        }
    }
}

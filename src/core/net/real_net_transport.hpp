#pragma once
#include "net_transport_interface.hpp"
#include <enet/enet.h>
#include <unordered_map>

namespace simnet::core::net::internal {
    /**
    * @brief Concrete transport implementation using ENet.
    *
    * All ENet calls are confined to this class.  External code talks to
    * INetTransport only, making it possible to swap in a mock for testing.
    */
    class RealNetTransport final : public INetTransport {
    public:
        RealNetTransport() = default;

        ~RealNetTransport() override;

        bool initialize_server(uint16_t port, size_t max_peers) override;

        bool initialize_client() override;

        void shutdown() override;

        PeerID connect(const std::string &address, uint16_t port) override;

        void disconnect(PeerID peer, DisconnectReason reason) override;

        void send(PeerID peer, const NetBuffer &buffer, uint8_t channel, TransportReliability reliability) override;

        void set_callbacks(TransportCallbacks callbacks) override;

        void service(utils::TimePoint now) override;

    private:
        ENetHost *host_ = nullptr;
        bool is_server_ = false;
        std::unordered_map<PeerID, ENetPeer *> id_to_peer_;
        std::unordered_map<ENetPeer *, PeerID> peer_ptr_to_id_;
        TransportCallbacks callbacks_;

        void on_enet_connect(const ENetEvent &event);

        void on_enet_disconnect(const ENetEvent &event);

        void on_enet_receive(const ENetEvent &event);

        void poll_peer_statistics();
    };
}

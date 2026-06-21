#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "net_buffer.hpp"
#include "net_types.hpp"
#include "core/utils/time_keeper.hpp"

namespace simnet::core::net::internal {
    enum class TransportReliability {
        reliable,
        unreliable_sequenced,
        unreliable_fragmented
    };

    struct TransportCallbacks {
        std::function<void(PeerID)> on_new_connection;
        std::function<void(PeerID, DisconnectReason)> on_disconnection;
        std::function<void(PeerID, NetBuffer &)> on_data;
    };

    class INetTransport {
    public:
        virtual ~INetTransport() = default;

        virtual bool initialize_server(uint16_t port, size_t max_peers) = 0;

        virtual bool initialize_client() = 0;

        virtual void shutdown() = 0;

        virtual PeerID connect(const std::string &address, uint16_t port) = 0;

        virtual void disconnect(PeerID peer, DisconnectReason reason) = 0;

        virtual void send(PeerID peer, const NetBuffer &buffer, uint8_t channel, TransportReliability reliability) = 0;

        virtual void set_callbacks(TransportCallbacks callbacks) = 0;

        virtual void service(utils::TimePoint now) = 0;
    };
}

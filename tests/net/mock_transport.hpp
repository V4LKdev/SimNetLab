#pragma once
#include "core/net/net_transport_interface.hpp"
#include "core/net/net_buffer.hpp"
#include "core/net/net_types.hpp"
#include <vector>
#include <cstdint>

namespace simnet::core::net::internal {
    class MockTransport final : public INetTransport {
    public:
        struct SendCall {
            PeerID peer;
            std::vector<uint8_t> data;
            uint8_t channel;
            TransportReliability reliability;
        };

        std::vector<SendCall> send_calls;

        struct DisconnectCall {
            PeerID peer;
            DisconnectReason reason;
        };

        std::vector<DisconnectCall> disconnect_calls;

        bool server_initialized = false;
        bool client_initialized = false;
        bool shutdown_called = false;
        int service_count = 0;

        TransportCallbacks callbacks;
        PeerID next_id = 1;

        bool initialize_server(uint16_t /*port*/, size_t /*max_peers*/) override
        {
            server_initialized = true;
            return true;
        }

        bool initialize_client() override
        {
            client_initialized = true;
            return true;
        }

        void shutdown() override { shutdown_called = true; }

        PeerID connect(const std::string & /*addr*/, uint16_t /*port*/) override
        {
            return next_id++;
        }

        void disconnect(PeerID peer, DisconnectReason reason) override
        {
            disconnect_calls.push_back({peer, reason});
        }

        void send(PeerID peer, const NetBuffer &buffer, uint8_t channel, TransportReliability reliability) override
        {
            send_calls.push_back({
                peer,
                {buffer.data(), buffer.data() + buffer.size()},
                channel,
                reliability
            });
        }

        void set_callbacks(TransportCallbacks cb) override
        {
            callbacks = std::move(cb);
        }

        void service(utils::TimePoint /*now*/, int /*timeout_ms*/) override
        {
            ++service_count;
        }

        // Helpers for simulating events
        void simulate_connect(PeerID id)
        {
            if (callbacks.on_new_connection) callbacks.on_new_connection(id);
        }

        void simulate_disconnect(PeerID id, DisconnectReason reason)
        {
            if (callbacks.on_disconnection) callbacks.on_disconnection(id, reason);
        }

        void simulate_data(PeerID id, NetBuffer &buffer)
        {
            if (callbacks.on_data) callbacks.on_data(id, buffer);
        }
    };
}

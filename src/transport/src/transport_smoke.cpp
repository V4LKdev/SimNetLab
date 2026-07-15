#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

import simnet.core;
import simnet.transport;

namespace
{
    constexpr auto smoke_port = std::uint16_t { 7788 };
    constexpr auto poll_timeout = std::chrono::seconds(2);

    [[nodiscard]] simnet::SessionIdentity identity(std::uint32_t protocol = 1)
    {
        return {
            .application_protocol_version = protocol,
            .compatibility_fingerprint = 0x1234U,
            .pipeline_decode_signature = 0x5678U,
            .capabilities = 0U,
        };
    }

    struct HandshakeResult
    {
        bool server_ready {};
        bool client_ready {};
        bool rejected {};
        simnet::PeerId server_peer {};
    };

    [[nodiscard]] bool poll_once(
        simnet::TransportServer& server,
        simnet::TransportClient& client,
        HandshakeResult& result)
    {
        auto events = std::vector<simnet::TransportEvent> {};
        auto poll = server.poll(events, 0);
        if (!poll.ok) {
            std::cerr << "server poll failed: " << poll.error.message << '\n';
            return false;
        }
        for (auto const& event : events) {
            if (auto const* ready = std::get_if<simnet::PeerSessionReady>(&event)) {
                result.server_ready = true;
                result.server_peer = ready->peer;
            } else if (std::holds_alternative<simnet::PeerDisconnected>(event)
                || std::holds_alternative<simnet::TransportErrorEvent>(event)) {
                result.rejected = true;
            }
        }

        events.clear();
        poll = client.poll(events, 1);
        if (!poll.ok) {
            std::cerr << "client poll failed: " << poll.error.message << '\n';
            return false;
        }
        for (auto const& event : events) {
            if (std::holds_alternative<simnet::PeerSessionReady>(event)) {
                result.client_ready = true;
            } else if (std::holds_alternative<simnet::PeerDisconnected>(event)
                || std::holds_alternative<simnet::TransportErrorEvent>(event)) {
                result.rejected = true;
            }
        }
        return true;
    }

    [[nodiscard]] bool poll_until(
        simnet::TransportServer& server,
        simnet::TransportClient& client,
        HandshakeResult& result,
        bool want_ready)
    {
        auto const deadline = std::chrono::steady_clock::now() + poll_timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!poll_once(server, client, result)) {
                return false;
            }
            if (want_ready && result.server_ready && result.client_ready) {
                return true;
            }
            if (!want_ready && result.rejected) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool matching_session_smoke()
    {
        auto server = simnet::TransportServer {};
        auto client = simnet::TransportClient {};
        auto limits = simnet::TransportLimits {
            .max_payload_bytes = 8U,
            .size_policy = simnet::SendSizePolicy::EnforceLimit,
        };

        auto result = server.start({
            .bind_address = "127.0.0.1",
            .port = smoke_port,
            .max_peers = 1U,
            .expected_identity = identity(),
            .limits = limits,
        });
        if (!result.ok) {
            std::cerr << "server start failed: " << result.error.message << '\n';
            return false;
        }

        result = client.connect({
            .server_address = "127.0.0.1",
            .server_port = smoke_port,
            .identity = identity(),
            .limits = limits,
        });
        if (!result.ok) {
            std::cerr << "client connect failed: " << result.error.message << '\n';
            return false;
        }

        auto handshake = HandshakeResult {};
        if (!poll_until(server, client, handshake, true)) {
            std::cerr << "matching session did not become ready\n";
            return false;
        }

        auto payload = std::array<simnet::Byte, 16> {};
        result = server.send({
            .peer = handshake.server_peer,
            .lane = simnet::Lane::Snapshot,
            .delivery = simnet::Delivery::ReliableSequenced,
            .payload = payload,
        });
        if (result.ok || result.error.code != simnet::TransportErrorCode::PayloadTooLarge) {
            std::cerr << "oversize send did not fail with PayloadTooLarge\n";
            return false;
        }

        client.disconnect(simnet::DisconnectCode::None);
        server.stop();
        return true;
    }

    [[nodiscard]] bool mismatched_session_smoke()
    {
        auto server = simnet::TransportServer {};
        auto client = simnet::TransportClient {};

        auto result = server.start({
            .bind_address = "127.0.0.1",
            .port = smoke_port,
            .max_peers = 1U,
            .expected_identity = identity(),
        });
        if (!result.ok) {
            std::cerr << "server start failed: " << result.error.message << '\n';
            return false;
        }

        result = client.connect({
            .server_address = "127.0.0.1",
            .server_port = smoke_port,
            .identity = identity(2U),
        });
        if (!result.ok) {
            std::cerr << "client connect failed: " << result.error.message << '\n';
            return false;
        }

        auto handshake = HandshakeResult {};
        if (!poll_until(server, client, handshake, false)) {
            std::cerr << "mismatched session did not reject\n";
            return false;
        }
        if (handshake.server_ready || handshake.client_ready) {
            std::cerr << "mismatched session became ready\n";
            return false;
        }

        client.disconnect(simnet::DisconnectCode::None);
        server.stop();
        return true;
    }

    [[nodiscard]] bool unsupported_backend_smoke()
    {
        auto server = simnet::TransportServer {};
        auto result = server.start({
            .backend = simnet::TransportBackend::LocalIpc,
            .bind_address = "127.0.0.1",
            .port = smoke_port,
            .max_peers = 1U,
            .expected_identity = identity(),
        });
        if (result.ok || result.error.code != simnet::TransportErrorCode::UnsupportedBackend) {
            std::cerr << "unsupported server backend was not rejected\n";
            return false;
        }

        auto client = simnet::TransportClient {};
        result = client.connect({
            .backend = simnet::TransportBackend::LocalIpc,
            .server_address = "127.0.0.1",
            .server_port = smoke_port,
            .identity = identity(),
        });
        if (result.ok || result.error.code != simnet::TransportErrorCode::UnsupportedBackend) {
            std::cerr << "unsupported client backend was not rejected\n";
            return false;
        }

        return true;
    }
}

int main()
{
    if (!matching_session_smoke()) {
        return 1;
    }
    if (!mismatched_session_smoke()) {
        return 1;
    }
    if (!unsupported_backend_smoke()) {
        return 1;
    }

    std::cout << "transport_smoke result=ok\n";
    return 0;
}

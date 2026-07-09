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
    constexpr auto smoke_ipc_path = "/tmp/simnet_transport_smoke.sock";
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

    struct SmokeSettings
    {
        simnet::TransportBackend backend { simnet::TransportBackend::ENet };
        std::uint16_t port { smoke_port };
        char const* ipc_path { smoke_ipc_path };
    };

    [[nodiscard]] simnet::TransportServerSettings server_settings(
        SmokeSettings const& settings,
        simnet::SessionIdentity expected_identity = identity())
    {
        return {
            .backend = settings.backend,
            .bind_address = "127.0.0.1",
            .local_ipc_path = settings.ipc_path,
            .port = settings.port,
            .max_peers = 1U,
            .expected_identity = expected_identity,
        };
    }

    [[nodiscard]] simnet::TransportClientSettings client_settings(
        SmokeSettings const& settings,
        simnet::SessionIdentity client_identity = identity())
    {
        return {
            .backend = settings.backend,
            .server_address = "127.0.0.1",
            .local_ipc_path = settings.ipc_path,
            .server_port = settings.port,
            .identity = client_identity,
        };
    }

    [[nodiscard]] bool matching_session_smoke(SmokeSettings const& settings)
    {
        auto server = simnet::TransportServer {};
        auto client = simnet::TransportClient {};
        auto limits = simnet::TransportLimits {
            .max_payload_bytes = 64U,
            .size_policy = simnet::SendSizePolicy::EnforceLimit,
        };

        auto server_config = server_settings(settings);
        server_config.limits = limits;
        auto result = server.start(server_config);
        if (!result.ok) {
            std::cerr << "server start failed: " << result.error.message << '\n';
            return false;
        }

        auto client_config = client_settings(settings);
        client_config.limits = limits;
        result = client.connect(client_config);
        if (!result.ok) {
            std::cerr << "client connect failed: " << result.error.message << '\n';
            return false;
        }

        auto handshake = HandshakeResult {};
        if (!poll_until(server, client, handshake, true)) {
            std::cerr << "matching session did not become ready\n";
            return false;
        }

        auto payload = std::array<simnet::Byte, 128> {};
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

        if (settings.backend == simnet::TransportBackend::LocalIpc) {
            auto small_payload = std::array<simnet::Byte, 4> {};
            result = server.send({
                .peer = handshake.server_peer,
                .lane = simnet::Lane::Snapshot,
                .delivery = simnet::Delivery::UnreliableSequenced,
                .payload = small_payload,
            });
            if (result.ok || result.error.code != simnet::TransportErrorCode::UnsupportedDelivery) {
                std::cerr << "local IPC unreliable send did not fail with UnsupportedDelivery\n";
                return false;
            }

            auto const ack_sent = client.send_snapshot_ack({
                .newest_received_snapshot = 1U,
                .received_mask = 0U,
                .newest_applied_snapshot = 1U,
            });
            if (!ack_sent.ok) {
                std::cerr << "local IPC snapshot ACK send failed: " << ack_sent.error.message << '\n';
                return false;
            }

            auto ack_seen = false;
            auto const deadline = std::chrono::steady_clock::now() + poll_timeout;
            while (!ack_seen && std::chrono::steady_clock::now() < deadline) {
                auto events = std::vector<simnet::TransportEvent> {};
                auto poll = server.poll(events, 10);
                if (!poll.ok) {
                    std::cerr << "server ACK poll failed: " << poll.error.message << '\n';
                    return false;
                }
                for (auto const& event : events) {
                    if (auto const* ack = std::get_if<simnet::SnapshotAckReceived>(&event)) {
                        ack_seen = ack->ack.newest_received_snapshot == 1U
                            && ack->ack.newest_applied_snapshot == 1U;
                    }
                }
            }
            if (!ack_seen) {
                std::cerr << "local IPC snapshot ACK was not received\n";
                return false;
            }
        }

        client.disconnect(simnet::DisconnectCode::None);
        server.stop();
        return true;
    }

    [[nodiscard]] bool mismatched_session_smoke(SmokeSettings const& settings)
    {
        auto server = simnet::TransportServer {};
        auto client = simnet::TransportClient {};

        auto result = server.start(server_settings(settings));
        if (!result.ok) {
            std::cerr << "server start failed: " << result.error.message << '\n';
            return false;
        }

        result = client.connect(client_settings(settings, identity(2U)));
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

#if !defined(SIMNET_ENABLE_LOCAL_IPC)
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
#endif
}

int main()
{
    auto const enet = SmokeSettings {};
    if (!matching_session_smoke(enet)) {
        return 1;
    }
    if (!mismatched_session_smoke(enet)) {
        return 1;
    }
#if defined(SIMNET_ENABLE_LOCAL_IPC)
    auto const local_ipc = SmokeSettings {
        .backend = simnet::TransportBackend::LocalIpc,
        .port = static_cast<std::uint16_t>(smoke_port + 1U),
        .ipc_path = smoke_ipc_path,
    };
    if (!matching_session_smoke(local_ipc)) {
        return 1;
    }
    if (!mismatched_session_smoke(local_ipc)) {
        return 1;
    }
#else
    if (!unsupported_backend_smoke()) {
        return 1;
    }
#endif

    std::cout << "transport_smoke result=ok\n";
    return 0;
}

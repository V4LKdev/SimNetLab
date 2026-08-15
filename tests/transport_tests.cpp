#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <variant>
#include <vector>

import simnet.core;
import simnet.transport;

namespace
{
    constexpr auto poll_timeout = std::chrono::seconds{2};
    constexpr auto port_range_min = std::uint16_t{20000U};
    constexpr auto port_range_size = std::uint16_t{20000U};
    constexpr auto max_port_attempts = std::size_t{64U};

    [[nodiscard]] std::uint16_t test_port()
    {
        auto const token =
            std::hash<std::thread::id>{}(std::this_thread::get_id()) ^
            static_cast<std::size_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        return static_cast<std::uint16_t>(
            port_range_min + (token % static_cast<std::uint64_t>(port_range_size))
        );
    }

    [[nodiscard]] simnet::SessionIdentity identity()
    {
        return {
            .application_protocol_version = 1U,
            .compatibility_fingerprint = 0x1234U,
            .application_wire_fingerprint = 0x5678U,
            .capabilities = 0U,
        };
    }

    [[nodiscard]] std::uint16_t start_server(
        simnet::TransportServer& server,
        simnet::SessionIdentity expected_identity = identity(),
        simnet::TransportLimits const* limits = nullptr
    )
    {
        auto config = simnet::TransportServerSettings{
            .bind_address = "127.0.0.1",
            .port = test_port(),
            .max_peers = 1U,
            .expected_identity = expected_identity,
        };
        if (limits != nullptr)
        {
            config.limits = *limits;
        }

        for (auto attempt = std::size_t{}; attempt < max_port_attempts; ++attempt)
        {
            auto const result = server.start(config);
            if (result.ok)
            {
                return config.port;
            }

            server.stop();
            ++config.port;
            if (config.port < port_range_min ||
                config.port >= static_cast<std::uint16_t>(port_range_min + port_range_size))
            {
                config.port = port_range_min;
            }
        }

        return 0U;
    }

    [[nodiscard]] simnet::TransportClientSettings client_settings(
        std::uint16_t port,
        simnet::SessionIdentity client_identity = identity()
    )
    {
        return {
            .server_address = "127.0.0.1",
            .server_port = port,
            .identity = client_identity,
        };
    }

    struct SessionResult
    {
        bool server_ready{};
        bool client_ready{};
        bool rejected{};
        simnet::PeerId server_peer{};
    };

    [[nodiscard]] SessionResult await_session(
        simnet::TransportServer& server,
        simnet::TransportClient& client
    )
    {
        auto result = SessionResult{};
        auto const deadline = std::chrono::steady_clock::now() + poll_timeout;

        while (std::chrono::steady_clock::now() < deadline)
        {
            auto events = std::vector<simnet::TransportEvent>{};

            auto poll = server.poll(events, 0);
            if (!poll.ok)
            {
                result.rejected = true;
                return result;
            }
            for (auto const& event : events)
            {
                if (auto const* ready = std::get_if<simnet::PeerSessionReady>(&event))
                {
                    result.server_ready = true;
                    result.server_peer = ready->peer;
                }
                else if (
                    std::holds_alternative<simnet::PeerDisconnected>(event) ||
                    std::holds_alternative<simnet::TransportErrorEvent>(event)
                )
                {
                    result.rejected = true;
                }
            }

            events.clear();
            poll = client.poll(events, 1);
            if (!poll.ok)
            {
                result.rejected = true;
                return result;
            }
            for (auto const& event : events)
            {
                if (std::holds_alternative<simnet::PeerSessionReady>(event))
                {
                    result.client_ready = true;
                }
                else if (
                    std::holds_alternative<simnet::PeerDisconnected>(event) ||
                    std::holds_alternative<simnet::TransportErrorEvent>(event)
                )
                {
                    result.rejected = true;
                }
            }

            if ((result.server_ready && result.client_ready) || result.rejected)
            {
                return result;
            }
        }

        return result;
    }

    [[nodiscard]] bool await_server_packet(
        simnet::TransportServer& server,
        simnet::TransportClient& client,
        simnet::PeerId peer,
        simnet::TransportLane lane,
        simnet::TransportDelivery delivery,
        simnet::ByteSpan expected
    )
    {
        auto const deadline = std::chrono::steady_clock::now() + poll_timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto events = std::vector<simnet::TransportEvent>{};
            static_cast<void>(client.poll(events, 0));
            events.clear();

            auto const poll = server.poll(events, 10);
            if (!poll.ok)
            {
                return false;
            }

            for (auto const& event : events)
            {
                if (auto const* packet = std::get_if<simnet::ReceivedPacket>(&event))
                {
                    if (packet->peer == peer && packet->lane == lane &&
                        packet->delivery == delivery &&
                        packet->payload == std::vector<simnet::Byte>{expected.begin(), expected.end()})
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    [[nodiscard]] bool session_is_rejected(simnet::SessionIdentity client_identity)
    {
        auto server = simnet::TransportServer{};
        auto client = simnet::TransportClient{};

        auto const port = start_server(server);
        if (port == 0U)
        {
            return false;
        }

        auto const connected = client.connect(client_settings(port, client_identity));
        if (!connected.ok)
        {
            server.stop();
            return false;
        }

        auto const session = await_session(server, client);
        client.disconnect(simnet::DisconnectCode::None);
        server.stop();

        return session.rejected && !session.server_ready && !session.client_ready;
    }
}

TEST_CASE("ENet establishes a matching session and preserves delivery semantics", "[transport][enet]")
{
    auto const limits = simnet::TransportLimits{
        .max_payload_bytes = 64U,
        .size_policy = simnet::SendSizePolicy::EnforceLimit,
    };

    auto server = simnet::TransportServer{};
    auto client = simnet::TransportClient{};

    auto const port = start_server(server, identity(), &limits);
    REQUIRE(port != 0U);

    auto config = client_settings(port);
    config.limits = limits;
    REQUIRE(client.connect(config).ok);

    auto const session = await_session(server, client);
    REQUIRE(session.server_ready);
    REQUIRE(session.client_ready);
    REQUIRE_FALSE(session.rejected);

    auto const payload = std::array{
        simnet::Byte{0x10U},
        simnet::Byte{0x20U},
        simnet::Byte{0x30U},
        simnet::Byte{0x40U},
    };

    REQUIRE(
        client.send(
            simnet::TransportLane::Lane0,
            simnet::TransportDelivery::ReliableSequenced,
            payload
        )
            .ok
    );
    CHECK(
        await_server_packet(
            server,
            client,
            session.server_peer,
            simnet::TransportLane::Lane0,
            simnet::TransportDelivery::ReliableSequenced,
            payload
        )
    );

    REQUIRE(
        client.send(
            simnet::TransportLane::Lane1,
            simnet::TransportDelivery::UnreliableSequenced,
            payload
        )
            .ok
    );
    CHECK(
        await_server_packet(
            server,
            client,
            session.server_peer,
            simnet::TransportLane::Lane1,
            simnet::TransportDelivery::UnreliableSequenced,
            payload
        )
    );

    auto const oversized = std::array<simnet::Byte, 128U>{};
    auto const rejected = client.send(
        simnet::TransportLane::Lane1,
        simnet::TransportDelivery::ReliableSequenced,
        oversized
    );
    CHECK_FALSE(rejected.ok);
    CHECK(rejected.error.code == simnet::TransportErrorCode::PayloadTooLarge);

    client.disconnect(simnet::DisconnectCode::None);
    server.stop();
}

TEST_CASE("ENet rejects incompatible session identities before readiness", "[transport][enet][identity]")
{
    auto protocol_mismatch = identity();
    ++protocol_mismatch.application_protocol_version;
    CHECK(session_is_rejected(protocol_mismatch));

    auto compatibility_mismatch = identity();
    compatibility_mismatch.compatibility_fingerprint ^= 1U;
    CHECK(session_is_rejected(compatibility_mismatch));

    auto wire_mismatch = identity();
    wire_mismatch.application_wire_fingerprint ^= 1U;
    CHECK(session_is_rejected(wire_mismatch));
}

TEST_CASE("ENet enforces the receiver payload limit", "[transport][enet][limits]")
{
    auto const server_limits = simnet::TransportLimits{
        .max_payload_bytes = 64U,
        .size_policy = simnet::SendSizePolicy::EnforceLimit,
    };
    auto const client_limits = simnet::TransportLimits{
        .max_payload_bytes = 64U,
        .size_policy = simnet::SendSizePolicy::AllowBackendFragmentation,
    };

    auto server = simnet::TransportServer{};
    auto client = simnet::TransportClient{};

    auto const port = start_server(server, identity(), &server_limits);
    REQUIRE(port != 0U);

    auto config = client_settings(port);
    config.limits = client_limits;
    REQUIRE(client.connect(config).ok);

    auto const session = await_session(server, client);
    REQUIRE(session.server_ready);
    REQUIRE(session.client_ready);

    auto const payload = std::array<simnet::Byte, 128U>{};
    REQUIRE(
        client.send(
            simnet::TransportLane::Lane1,
            simnet::TransportDelivery::ReliableSequenced,
            payload
        )
            .ok
    );

    auto rejected = false;
    auto received = false;
    auto const deadline = std::chrono::steady_clock::now() + poll_timeout;
    while (!rejected && std::chrono::steady_clock::now() < deadline)
    {
        auto events = std::vector<simnet::TransportEvent>{};
        static_cast<void>(client.poll(events, 0));
        events.clear();

        auto const poll = server.poll(events, 10);
        if (!poll.ok)
        {
            rejected = poll.error.code == simnet::TransportErrorCode::PayloadTooLarge;
            continue;
        }

        for (auto const& event : events)
        {
            received = received || std::holds_alternative<simnet::ReceivedPacket>(event);
            rejected = rejected ||
                       std::holds_alternative<simnet::TransportErrorEvent>(event) ||
                       std::holds_alternative<simnet::PeerDisconnected>(event);
        }
    }

    CHECK_FALSE(received);
    CHECK(rejected);
    CHECK(server.stats().oversize_drops > 0U);

    client.disconnect(simnet::DisconnectCode::None);
    server.stop();
}

TEST_CASE(
    "ENet unreliable sequenced delivery rejects implicit backend fragmentation",
    "[transport][enet][fragmentation]"
)
{
    auto const limits = simnet::TransportLimits{
        .max_payload_bytes = 4096U,
        .size_policy = simnet::SendSizePolicy::AllowBackendFragmentation,
    };

    auto server = simnet::TransportServer{};
    auto client = simnet::TransportClient{};

    auto const port = start_server(server, identity(), &limits);
    REQUIRE(port != 0U);

    auto config = client_settings(port);
    config.limits = limits;
    REQUIRE(client.connect(config).ok);

    auto const session = await_session(server, client);
    REQUIRE(session.server_ready);
    REQUIRE(session.client_ready);

    auto const payload = std::array<simnet::Byte, 2000U>{};

    auto const unreliable = client.send(
        simnet::TransportLane::Lane1,
        simnet::TransportDelivery::UnreliableSequenced,
        payload
    );
    CHECK_FALSE(unreliable.ok);
    CHECK(unreliable.error.code == simnet::TransportErrorCode::PayloadTooLarge);

    CHECK(
        client.send(
            simnet::TransportLane::Lane1,
            simnet::TransportDelivery::ReliableSequenced,
            payload
        )
            .ok
    );

    client.disconnect(simnet::DisconnectCode::None);
    server.stop();
}
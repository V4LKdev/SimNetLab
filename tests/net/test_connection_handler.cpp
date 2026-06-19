#include <catch2/catch_test_macros.hpp>
#include "net/connection_handler.hpp"
#include "net/net_message.hpp"
#include "net/net_types.hpp"
#include "mock_transport.hpp"
#include "test_helpers.hpp"

using simnet::core::net::internal::ConnectionHandler;
using simnet::core::net::internal::PeerID;
using simnet::core::net::internal::DisconnectReason;
using simnet::core::net::internal::RejectReason;
using simnet::core::net::internal::MessageType;
using simnet::core::net::internal::ProtocolVersion;
using simnet::core::net::internal::CURRENT_PROTOCOL_VERSION;
using simnet::core::net::internal::NetBuffer;
using simnet::core::net::internal::MockTransport;
using simnet::core::utils::Milliseconds;
using simnet::core::utils::TimePoint;

namespace {
    Milliseconds SHORT_TIMEOUT{50};
    Milliseconds PING_INTERVAL{30};

    NetBuffer build_hello(ProtocolVersion ver = CURRENT_PROTOCOL_VERSION)
    {
        NetBuffer buf;
        buf.write(static_cast<uint8_t>(MessageType::Hello));
        buf.write(ver);
        return buf;
    }

    NetBuffer build_welcome()
    {
        NetBuffer buf;
        buf.write(static_cast<uint8_t>(MessageType::Welcome));
        return buf;
    }

    NetBuffer build_reject(RejectReason reason)
    {
        NetBuffer buf;
        buf.write(static_cast<uint8_t>(MessageType::Reject));
        buf.write(static_cast<uint8_t>(reason));
        return buf;
    }

    NetBuffer build_ping()
    {
        NetBuffer buf;
        buf.write(static_cast<uint8_t>(MessageType::Ping));
        return buf;
    }
}

TEST_CASE("ConnectionHandler: valid hello as server", "[connection_handler]")
{
    MockTransport transport;
    ConnectionHandler handler(transport, PING_INTERVAL, SHORT_TIMEOUT);
    handler.set_role(true); // server
    bool connected_called = false;
    handler.on_connected = [&](PeerID id) {
        connected_called = true;
        REQUIRE(id == 1);
    };

    handler.on_transport_connect(1);
    REQUIRE(handler.find_peer(1) != nullptr);

    auto hello_buf = build_hello();
    handler.on_transport_data(1, hello_buf);

    // Verify Welcome sent
    REQUIRE(transport.send_calls.size() >= 1);
    // First byte of buffer should be Welcome type
    REQUIRE(transport.send_calls[0].data[0] == static_cast<uint8_t>(MessageType::Welcome));
    REQUIRE(connected_called);
    REQUIRE(handler.find_peer(1)->is_handshake_complete());
}

TEST_CASE("ConnectionHandler: version mismatch hello", "[connection_handler]")
{
    MockTransport transport;
    ConnectionHandler handler(transport, PING_INTERVAL, SHORT_TIMEOUT);
    handler.set_role(true); // server
    bool connected = false;
    handler.on_connected = [&](PeerID) { connected = true; };

    handler.on_transport_connect(1);
    auto buf = build_hello(CURRENT_PROTOCOL_VERSION + 1);
    handler.on_transport_data(1, buf);

    // Should have sent reject
    REQUIRE(!transport.send_calls.empty());
    REQUIRE(transport.send_calls[0].data[0] == static_cast<uint8_t>(MessageType::Reject));
    // Transport disconnect called with Rejected
    REQUIRE(transport.disconnect_calls.size() == 1);
    REQUIRE(transport.disconnect_calls[0].reason == DisconnectReason::Rejected);
    REQUIRE(!connected);
}

TEST_CASE("ConnectionHandler: client receives welcome", "[connection_handler]")
{
    MockTransport transport;
    ConnectionHandler handler(transport, PING_INTERVAL, SHORT_TIMEOUT);
    handler.set_role(false); // client

    handler.register_outgoing_peer(1);
    handler.on_transport_connect(1);

    bool connected = false;
    handler.on_connected = [&](PeerID) { connected = true; };

    auto welcome = build_welcome();
    handler.on_transport_data(1, welcome);
    REQUIRE(connected);
    REQUIRE(handler.find_peer(1)->is_handshake_complete());
}

TEST_CASE("ConnectionHandler: client receives reject", "[connection_handler]")
{
    MockTransport transport;
    ConnectionHandler handler(transport, PING_INTERVAL, SHORT_TIMEOUT);
    handler.set_role(false);

    handler.register_outgoing_peer(1);
    handler.on_transport_connect(1);

    bool rejected = false;
    RejectReason reason = RejectReason::Other;
    handler.on_rejected = [&](PeerID id, RejectReason r) {
        rejected = true;
        reason = r;
    };

    auto reject_buf = build_reject(RejectReason::ServerFull);
    handler.on_transport_data(1, reject_buf);
    REQUIRE(rejected);
    REQUIRE(reason == RejectReason::ServerFull);
}

TEST_CASE("ConnectionHandler: ping triggers pong", "[connection_handler]")
{
    MockTransport transport;
    ConnectionHandler handler(transport, PING_INTERVAL, SHORT_TIMEOUT);
    handler.set_role(true);

    handler.on_transport_connect(1);
    auto hello = build_hello(); // complete handshake
    handler.on_transport_data(1, hello);

    transport.send_calls.clear(); // ignore handshake
    auto ping = build_ping();
    handler.on_transport_data(1, ping);
    REQUIRE(!transport.send_calls.empty());
    REQUIRE(transport.send_calls[0].data[0] == static_cast<uint8_t>(MessageType::Pong));
}

TEST_CASE("ConnectionHandler: pings sent periodically", "[connection_handler]")
{
    MockTransport transport;
    ConnectionHandler handler(transport, PING_INTERVAL, SHORT_TIMEOUT);
    handler.set_role(true);
    // Simulate connected peer (handshake complete)
    handler.on_transport_connect(1);
    auto hello = build_hello();
    handler.on_transport_data(1, hello);
    transport.send_calls.clear();

    auto now = make_time(0);
    handler.update(now);
    REQUIRE(transport.send_calls.empty()); // no ping yet

    now = make_time(PING_INTERVAL.count() + 5);
    handler.update(now);
    // Should have sent a ping
    auto ping_itr = std::find_if(transport.send_calls.begin(), transport.send_calls.end(),
                                 [](const auto &sc) { return sc.data[0] == static_cast<uint8_t>(MessageType::Ping); });
    REQUIRE(ping_itr != transport.send_calls.end());

    // Second update soon after should not send another
    transport.send_calls.clear();
    handler.update(now + Milliseconds(1));
    ping_itr = std::find_if(transport.send_calls.begin(), transport.send_calls.end(),
                            [](const auto &sc) { return sc.data[0] == static_cast<uint8_t>(MessageType::Ping); });
    REQUIRE(ping_itr == transport.send_calls.end());
}

TEST_CASE("ConnectionHandler: connecting peer timeout", "[connection_handler]")
{
    MockTransport transport;
    ConnectionHandler handler(transport, PING_INTERVAL, SHORT_TIMEOUT);
    handler.register_outgoing_peer(1);

    // Force ancient activity time – peer added but activity time is epoch
    // update just to set current_time_
    handler.update(make_time(0));
    // Explicitly set activity to ancient
    handler.record_peer_activity(1, make_time(-1000));

    bool disconnected = false;
    handler.on_disconnected = [&](PeerID id, DisconnectReason r) {
        disconnected = true;
        REQUIRE(id == 1);
        REQUIRE(r == DisconnectReason::Timeout);
    };

    // Any update with a recent time will trigger timeout
    handler.update(make_time(0));
    REQUIRE(!transport.disconnect_calls.empty());
    REQUIRE(transport.disconnect_calls[0].reason == DisconnectReason::Timeout);
    REQUIRE(disconnected);
}


TEST_CASE("ConnectionHandler: handshaking timeout does not fire callback immediately", "[connection_handler]")
{
    MockTransport transport;
    ConnectionHandler handler(transport, PING_INTERVAL, SHORT_TIMEOUT);
    handler.set_role(true);
    handler.on_transport_connect(1);
    // Force ancient activity time
    handler.update(make_time(0));
    handler.record_peer_activity(1, make_time(-1000));

    bool disconnected = false;
    handler.on_disconnected = [&](PeerID, DisconnectReason) { disconnected = true; };

    handler.update(make_time(0));
    REQUIRE(!transport.disconnect_calls.empty());
    // Callback NOT fired – only transport disconnect called
    REQUIRE(!disconnected);
}


TEST_CASE("ConnectionHandler: record_peer_activity prevents timeout", "[connection_handler]")
{
    MockTransport transport;
    ConnectionHandler handler(transport, PING_INTERVAL, SHORT_TIMEOUT);
    handler.register_outgoing_peer(1);
    auto now = make_time(0);
    // Override the wall‑clock activity time so we control it
    handler.record_peer_activity(1, now);
    handler.update(now);

    // Advance almost to timeout, then touch activity
    now = make_time(SHORT_TIMEOUT.count() - 1);
    handler.record_peer_activity(1, now);
    handler.update(now);
    REQUIRE(transport.disconnect_calls.empty());

    // Now advance far past the last activity
    now += SHORT_TIMEOUT + Milliseconds(5);
    handler.update(now);
    REQUIRE(!transport.disconnect_calls.empty());
}


TEST_CASE("ConnectionHandler: duplicate disconnect callback suppression", "[connection_handler]")
{
    MockTransport transport;
    ConnectionHandler handler(transport, PING_INTERVAL, SHORT_TIMEOUT);
    handler.set_role(true);
    handler.on_transport_connect(1);
    auto hello1 = build_hello();
    handler.on_transport_data(1, hello1);

    int disconnect_count = 0;
    handler.on_disconnected = [&](PeerID, DisconnectReason) { ++disconnect_count; };

    // First disconnect
    handler.on_transport_disconnect(1, DisconnectReason::ClientQuit);
    REQUIRE(disconnect_count == 1);
    // Second disconnect (duplicate)
    handler.on_transport_disconnect(1, DisconnectReason::ClientQuit);
    REQUIRE(disconnect_count == 1);

    // Fresh connection – build a new hello buffer
    handler.on_transport_connect(1);
    auto hello2 = build_hello();
    handler.on_transport_data(1, hello2);
    handler.on_transport_disconnect(1, DisconnectReason::ClientQuit);
    REQUIRE(disconnect_count == 2);
}


TEST_CASE("ConnectionHandler: find_peer and get_peer_state", "[connection_handler]")
{
    MockTransport transport;
    ConnectionHandler handler(transport, PING_INTERVAL, SHORT_TIMEOUT);
    REQUIRE(handler.find_peer(42) == nullptr);
    handler.register_outgoing_peer(42);
    REQUIRE(handler.find_peer(42) != nullptr);
    REQUIRE(handler.get_peer_state(42).get_id() == 42);
    REQUIRE_THROWS(handler.get_peer_state(100));
}

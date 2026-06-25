// test_connection_handler.cpp
#include <catch2/catch_test_macros.hpp>
#include "core/net/net_connection_handler.hpp"
#include "core/net/net_message.hpp"
#include "core/net/net_types.hpp"
#include "mock_transport.hpp"
#include "test_helpers.hpp"
#include "core/config/sim_config.hpp"

using simnet::core::net::internal::ConnectionHandler;
using simnet::core::net::internal::PeerID;
using simnet::core::net::internal::DisconnectReason;
using simnet::core::net::internal::RejectReason;
using simnet::core::net::internal::MessageType;
using simnet::core::net::internal::ProtocolVersion;
using simnet::core::net::internal::CURRENT_PROTOCOL_VERSION;
using simnet::core::net::internal::NetBuffer;
using simnet::core::net::internal::MockTransport;

namespace {
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
}

TEST_CASE("ConnectionHandler: valid hello as server", "[connection_handler]")
{
    auto config = default_test_config();
    MockTransport transport;
    ConnectionHandler handler(transport, config);
    handler.set_role(true);
    bool connected_called = false;
    handler.on_connected = [&](PeerID id) {
        connected_called = true;
        REQUIRE(id == 1);
    };

    handler.on_transport_connect(1);
    REQUIRE(handler.find_peer(1) != nullptr);

    auto hello_buf = build_hello_buf(CURRENT_PROTOCOL_VERSION, config.fingerprint());
    handler.on_transport_data(1, hello_buf);

    REQUIRE(!transport.send_calls.empty());
    REQUIRE(transport.send_calls[0].data[0] == static_cast<uint8_t>(MessageType::Welcome));
    REQUIRE(connected_called);
    REQUIRE(handler.find_peer(1)->is_handshake_complete());
}

TEST_CASE("ConnectionHandler: version mismatch hello", "[connection_handler]")
{
    auto config = default_test_config();
    MockTransport transport;
    ConnectionHandler handler(transport, config);
    handler.set_role(true);
    bool connected = false;
    handler.on_connected = [&](PeerID) { connected = true; };

    handler.on_transport_connect(1);
    auto hello_buf = build_hello_buf(CURRENT_PROTOCOL_VERSION + 1, config.fingerprint());
    handler.on_transport_data(1, hello_buf);

    REQUIRE(!transport.send_calls.empty());
    REQUIRE(transport.send_calls[0].data[0] == static_cast<uint8_t>(MessageType::Reject));
    REQUIRE(transport.disconnect_calls.size() == 1);
    REQUIRE(transport.disconnect_calls[0].reason == DisconnectReason::Rejected);
    REQUIRE(!connected);
}

TEST_CASE("ConnectionHandler: client receives welcome", "[connection_handler]")
{
    auto config = default_test_config();
    MockTransport transport;
    ConnectionHandler handler(transport, config);
    handler.set_role(false);

    handler.register_outgoing_peer(1);
    handler.on_transport_connect(1);

    REQUIRE(transport.send_calls.size() == 1);
    REQUIRE(transport.send_calls[0].data[0] == static_cast<uint8_t>(MessageType::Hello));
    transport.send_calls.clear();

    bool connected = false;
    handler.on_connected = [&](PeerID) { connected = true; };

    auto welcome = build_welcome();
    handler.on_transport_data(1, welcome);
    REQUIRE(connected);
    REQUIRE(handler.find_peer(1)->is_handshake_complete());
}

TEST_CASE("ConnectionHandler: client receives reject", "[connection_handler]")
{
    auto config = default_test_config();
    MockTransport transport;
    ConnectionHandler handler(transport, config);
    handler.set_role(false);

    handler.register_outgoing_peer(1);
    handler.on_transport_connect(1);

    REQUIRE(transport.send_calls.size() == 1);
    REQUIRE(transport.send_calls[0].data[0] == static_cast<uint8_t>(MessageType::Hello));
    transport.send_calls.clear();

    bool rejected = false;
    RejectReason reason = RejectReason::Other;
    handler.on_rejected = [&](PeerID, RejectReason r) {
        rejected = true;
        reason = r;
    };

    auto reject_buf = build_reject(RejectReason::ServerFull);
    handler.on_transport_data(1, reject_buf);
    REQUIRE(rejected);
    REQUIRE(reason == RejectReason::ServerFull);
}

TEST_CASE("ConnectionHandler: duplicate disconnect callback suppression", "[connection_handler]")
{
    auto config = default_test_config();
    MockTransport transport;
    ConnectionHandler handler(transport, config);
    handler.set_role(true);
    handler.on_transport_connect(1);
    auto hello1 = build_hello_buf(CURRENT_PROTOCOL_VERSION, config.fingerprint());
    handler.on_transport_data(1, hello1);

    int disconnect_count = 0;
    handler.on_disconnected = [&](PeerID, DisconnectReason) { ++disconnect_count; };

    handler.on_transport_disconnect(1, DisconnectReason::ClientQuit);
    REQUIRE(disconnect_count == 1);
    handler.on_transport_disconnect(1, DisconnectReason::ClientQuit);
    REQUIRE(disconnect_count == 1);

    handler.on_transport_connect(1);
    auto hello2 = build_hello_buf(CURRENT_PROTOCOL_VERSION, config.fingerprint());
    handler.on_transport_data(1, hello2);
    handler.on_transport_disconnect(1, DisconnectReason::ClientQuit);
    REQUIRE(disconnect_count == 2);
}

TEST_CASE("ConnectionHandler: find_peer and get_peer_state", "[connection_handler]")
{
    auto config = default_test_config();
    MockTransport transport;
    ConnectionHandler handler(transport, config);
    REQUIRE(handler.find_peer(42) == nullptr);
    handler.register_outgoing_peer(42);
    REQUIRE(handler.find_peer(42) != nullptr);
    REQUIRE(handler.get_peer_state(42).get_id() == 42);
    REQUIRE_THROWS(handler.get_peer_state(100));
}

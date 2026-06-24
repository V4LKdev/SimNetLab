#include <catch2/catch_test_macros.hpp>
#include "core/net/net_manager.hpp"
#include "core/net/net_snapshot.hpp"
#include "core/net/net_message.hpp"
#include "mock_transport.hpp"
#include "test_helpers.hpp"
#include <memory>

using simnet::core::net::NetManager;
using simnet::core::net::NetRole;
using simnet::core::net::PeerID;
using simnet::core::net::DisconnectReason;
using simnet::core::net::RejectReason;
using simnet::core::net::internal::MockTransport;
using simnet::core::net::internal::NetBuffer;
using simnet::core::net::internal::MessageType;
using simnet::core::net::internal::ReplicationSnapshot;
using simnet::core::net::internal::CURRENT_PROTOCOL_VERSION;

static NetBuffer build_snapshot_buffer(ReplicationSnapshot &snap)
{
    NetBuffer buf;
    snap.write(buf);
    return buf;
}

namespace {
    class NetManagerTestFixture {
    public:
        simnet::core::config::SimConfig config = default_test_config();
        std::unique_ptr<NetManager> net;
        MockTransport *mockTransport = nullptr;

        void init_as_server()
        {
            net = std::make_unique<NetManager>(NetRole::server, config);
            auto mock = std::make_unique<MockTransport>();
            mockTransport = mock.get();
            net->set_transport_for_testing(std::move(mock));
        }

        void init_as_client()
        {
            net = std::make_unique<NetManager>(NetRole::client, config);
            auto mock = std::make_unique<MockTransport>();
            mockTransport = mock.get();
            net->set_transport_for_testing(std::move(mock));
        }

        // Helper: perform a full handshake for a given peer ID
        void handshake_peer(PeerID id)
        {
            mockTransport->simulate_connect(id);
            auto hello = build_hello_buf(CURRENT_PROTOCOL_VERSION, config.fingerprint());
            mockTransport->simulate_data(id, hello);
            net->update(make_time(0), 0); // flush, optional
        }
    };
}

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: constructor does not throw", "[net_manager]")
{
    REQUIRE_NOTHROW(init_as_server());
}

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: update calls service", "[net_manager]")
{
    init_as_server();
    auto now = make_time(0);
    net->update(now, 0);
    REQUIRE(mockTransport->service_count > 0);
}

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: client connect registers outgoing peer", "[net_manager]")
{
    init_as_client();
    auto id = net->connect("127.0.0.1", 7777);
    REQUIRE(id != 0);
    REQUIRE_NOTHROW(mockTransport->simulate_connect(id));
}

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: snapshot dispatch fires callback", "[net_manager]")
{
    init_as_server();
    const PeerID peerId = 1;
    handshake_peer(peerId);

    bool snapshot_received = false;
    net->set_snapshot_callback([&](const ReplicationSnapshot &s) {
        snapshot_received = true;
        REQUIRE(s.tick == 42);
    });

    ReplicationSnapshot snap;
    snap.tick = 42;
    auto snapBuf = build_snapshot_buffer(snap);
    mockTransport->simulate_data(peerId, snapBuf);
    REQUIRE(snapshot_received);
}

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: snapshot from missing peer does not crash", "[net_manager]")
{
    init_as_server();
    ReplicationSnapshot snap;
    auto buf = build_snapshot_buffer(snap);
    REQUIRE_NOTHROW(mockTransport->simulate_data(999, buf));
}

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: broadcast snapshot sends to connected peers", "[net_manager]")
{
    init_as_server();
    handshake_peer(1);
    mockTransport->send_calls.clear(); // ignore Welcome

    ReplicationSnapshot snap;
    snap.tick = 10;
    net->broadcast_snapshot(snap);
    REQUIRE(mockTransport->send_calls.size() == 1);
    REQUIRE(mockTransport->send_calls[0].channel == static_cast<uint8_t>(
        simnet::core::net::internal::NetChannel::Snapshot));
    REQUIRE(mockTransport->send_calls[0].data[0] == static_cast<uint8_t>(MessageType::Snapshot));
}

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: callbacks propagate from connection handler", "[net_manager]")
{
    init_as_server();
    bool connected = false;
    net->set_on_connected([&](PeerID) { connected = true; });
    handshake_peer(1);
    REQUIRE(connected);
}

#include <catch2/catch_test_macros.hpp>
#include "net/net_manager.hpp"
#include "net/net_snapshot.hpp"
#include "net/net_message.hpp"
#include "mock_transport.hpp"
#include "test_helpers.hpp"
#include <memory>

using simnet::core::net::NetManager;
using simnet::core::net::NetRole;
using simnet::core::net::NetConfig;
using simnet::core::net::PeerID;
using simnet::core::net::DisconnectReason;
using simnet::core::net::RejectReason;
using simnet::core::net::internal::INetTransport;
using simnet::core::net::internal::MockTransport;
using simnet::core::net::internal::NetBuffer;
using simnet::core::net::internal::MessageType;
using simnet::core::net::internal::SnapshotFlags;
using simnet::core::net::internal::ReplicationSnapshot;
using simnet::core::net::internal::ReplicatedEntity;
using simnet::core::net::internal::CURRENT_PROTOCOL_VERSION;

// ---------- Helper to build a snapshot buffer for simulation ----------
static NetBuffer build_snapshot_buffer(ReplicationSnapshot &snap)
{
    NetBuffer buf;
    snap.write(buf);
    return buf;
}

// ---------- Fixture that injects a mock transport ----------
class NetManagerTestFixture {
public:
    NetManager net;
    MockTransport *mockTransport = nullptr;

    NetManagerTestFixture()
    {
        auto mock = std::make_unique<MockTransport>();
        mockTransport = mock.get();
        net.set_transport_for_testing(std::move(mock));
    }
};


// Ensure set_transport_for_testing exists in NetManager; otherwise add it.
// If not added, tests below will not compile.

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: initialization as server", "[net_manager]")
{
    NetConfig cfg;
    cfg.port = 7777;
    cfg.max_peers = 10;
    REQUIRE(net.initialize(NetRole::server, cfg));
    REQUIRE(mockTransport->server_initialized);
}


// The following tests rely on the fixture pattern.
TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: update calls service and connection handler update",
                 "[net_manager]")
{
    net.initialize(NetRole::server, NetConfig{0, 10, 0});
    auto now = make_time(0);
    net.update(now);
    // Service should have been called at least once
    REQUIRE(mockTransport->service_count > 0);
    // We could also verify that conn_handler->update processed, but indirectly via pings later.
}

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: client connect registers outgoing peer", "[net_manager]")
{
    net.initialize(NetRole::client, NetConfig{0, 0, 2});
    auto id = net.connect("127.0.0.1", 7777);
    REQUIRE(id != 0);
    // We can't access conn_handler directly from outside, but mock's connect was called.
    // To test that register_outgoing_peer happened, we'd need to inspect internal state.
    // Instead, we verify that a subsequent transport connect event doesn't crash.
    REQUIRE_NOTHROW(mockTransport->simulate_connect(id));
}

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: snapshot dispatch marks peer activity", "[net_manager]")
{
    NetConfig cfg;
    cfg.max_peers = 10;
    cfg.timeout_ms = 100; // short timeout for test
    net.initialize(NetRole::server, cfg);

    const PeerID peerId = 1;
    mockTransport->simulate_connect(peerId);
    // Send Hello to complete handshake
    NetBuffer helloBuf;
    helloBuf.write(static_cast<uint8_t>(MessageType::Hello));
    helloBuf.write(CURRENT_PROTOCOL_VERSION);
    mockTransport->simulate_data(peerId, helloBuf);

    // Prepare a snapshot buffer
    ReplicationSnapshot snap;
    snap.tick = 100;
    auto snapBuf = build_snapshot_buffer(snap);

    // Advance time near the timeout
    auto now = make_time(0);
    net.update(now); // set current_time in conn_handler
    now = make_time(80); // still within 100 ms timeout
    net.update(now);
    mockTransport->simulate_data(peerId, snapBuf); // snapshot should record activity at time ~80

    // Now advance past the timeout from start, but not from the snapshot's activity
    now = make_time(150); // 150 ms since epoch, peer activity recorded at ~80, diff 70 ms < 100 ms timeout
    net.update(now);
    // No disconnect should have happened because snapshot kept the peer alive
    REQUIRE(mockTransport->disconnect_calls.empty());
}

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: snapshot dispatch fires callback", "[net_manager]")
{
    net.initialize(NetRole::server, NetConfig{0, 10, 0});
    const PeerID peerId = 1;
    mockTransport->simulate_connect(peerId);
    NetBuffer helloBuf;
    helloBuf.write(static_cast<uint8_t>(MessageType::Hello));
    helloBuf.write(CURRENT_PROTOCOL_VERSION);
    mockTransport->simulate_data(peerId, helloBuf);

    ReplicationSnapshot snap;
    snap.tick = 42;
    auto snapBuf = build_snapshot_buffer(snap);
    net.update(make_time(0)); // needed to set current_time in connection handler

    bool snapshot_received = false;
    net.set_snapshot_callback([&](const ReplicationSnapshot &s) {
        snapshot_received = true;
        REQUIRE(s.tick == 42);
    });

    mockTransport->simulate_data(peerId, snapBuf);
    REQUIRE(snapshot_received);
}


TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: snapshot from missing peer does not crash", "[net_manager]")
{
    net.initialize(NetRole::server, NetConfig{0, 10, 0});
    ReplicationSnapshot snap;
    auto buf = build_snapshot_buffer(snap);
    REQUIRE_NOTHROW(mockTransport->simulate_data(999, buf));
}

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: broadcast snapshot sends to connected peers", "[net_manager]")
{
    net.initialize(NetRole::server, NetConfig{0, 10, 0});
    // create connected peer 1
    mockTransport->simulate_connect(1);
    NetBuffer helloBuf;
    helloBuf.write(static_cast<uint8_t>(MessageType::Hello));
    helloBuf.write(CURRENT_PROTOCOL_VERSION);
    mockTransport->simulate_data(1, helloBuf);

    // Clear handshake send calls (Welcome)
    mockTransport->send_calls.clear();

    ReplicationSnapshot snap;
    snap.tick = 10;
    net.broadcast_snapshot(snap);
    REQUIRE(mockTransport->send_calls.size() == 1);
    REQUIRE(mockTransport->send_calls[0].channel == static_cast<uint8_t>(
        simnet::core::net::internal::NetChannel::GameplayUnreliable));
    // Verify snapshot header in payload
    REQUIRE(mockTransport->send_calls[0].data[0] == static_cast<uint8_t>(MessageType::Snapshot));
}

TEST_CASE_METHOD(NetManagerTestFixture, "NetManager: callbacks propagate from connection handler", "[net_manager]")
{
    net.initialize(NetRole::server, NetConfig{0, 10, 0});
    bool connected = false;
    net.set_on_connected([&](PeerID) { connected = true; });
    // simulate handshake that leads to on_connected inside ConnectionHandler
    mockTransport->simulate_connect(1);
    NetBuffer helloBuf;
    helloBuf.write(static_cast<uint8_t>(MessageType::Hello));
    helloBuf.write(CURRENT_PROTOCOL_VERSION);
    mockTransport->simulate_data(1, helloBuf);
    REQUIRE(connected);
}

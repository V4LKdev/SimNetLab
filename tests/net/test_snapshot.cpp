#include <catch2/catch_test_macros.hpp>
#include "net/net_snapshot.hpp"
#include "net/net_buffer.hpp"
#include "net/net_types.hpp"
#include <stdexcept>

using simnet::core::net::internal::ReplicationSnapshot;
using simnet::core::net::internal::ReplicatedEntity;
using simnet::core::net::internal::MessageType;
using simnet::core::net::internal::SnapshotFlags;
using simnet::core::net::internal::NetBuffer;

TEST_CASE("ReplicationSnapshot: roundtrip with entities", "[snapshot]")
{
    ReplicationSnapshot original;
    original.tick = 42;
    original.sequence = 10;
    ReplicatedEntity ent;
    ent.network_id = 1;
    ent.position = simnet::core::math::Vec3(1.0f, 2.0f, 3.0f);
    ent.heading = simnet::core::math::Vec3(0.1f, 0.2f, 0.3f);
    ent.hue = 128;
    original.entities.push_back(ent);

    NetBuffer buf;
    original.write(buf);

    auto restored = ReplicationSnapshot::read(buf);
    REQUIRE(restored.tick == 42);
    REQUIRE(restored.sequence == 10);
    REQUIRE(restored.entities.size() == 1);
    REQUIRE(restored.entities[0].network_id == 1);
    REQUIRE(restored.entities[0].hue == 128);
    // Vec3 equality – assumes operator== exists; if not, we can check components.
}

TEST_CASE("ReplicationSnapshot: empty snapshot roundtrip", "[snapshot]")
{
    ReplicationSnapshot snap;
    NetBuffer buf;
    snap.write(buf);
    auto restored = ReplicationSnapshot::read(buf);
    REQUIRE(restored.entities.empty());
}

TEST_CASE("ReplicationSnapshot: wrong message type throws", "[snapshot]")
{
    NetBuffer buf;
    buf.write(static_cast<uint8_t>(MessageType::Hello)); // not Snapshot
    REQUIRE_THROWS_AS(ReplicationSnapshot::read(buf), std::runtime_error);
}

#include <catch2/catch_test_macros.hpp>
#include "core/net/net_snapshot.hpp"
#include "core/net/net_buffer.hpp"
#include "core/net/net_types.hpp"
#include <stdexcept>
#include <catch2/catch_approx.hpp>

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
    REQUIRE(restored.entities[0].position.x() == Catch::Approx(1.0f));
    REQUIRE(restored.entities[0].position.y() == Catch::Approx(2.0f));
    REQUIRE(restored.entities[0].position.z() == Catch::Approx(3.0f));
    REQUIRE(restored.entities[0].heading.x() == Catch::Approx(0.1f));
    REQUIRE(restored.entities[0].heading.y() == Catch::Approx(0.2f));
    REQUIRE(restored.entities[0].heading.z() == Catch::Approx(0.3f));
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

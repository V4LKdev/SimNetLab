#include <catch2/catch_test_macros.hpp>

import simnet.core;
import simnet.snapshot;

namespace
{
    [[nodiscard]] simnet::EntityState entity(simnet::EntityNetId id)
    {
        return {
            .id = id,
            .heading = {1.0F, 0.0F, 0.0F},
        };
    }

    [[nodiscard]] simnet::WorldSnapshot snapshot(simnet::EntityNetId id)
    {
        auto result = simnet::WorldSnapshot{};
        result.ids.push_back(id);
        result.positions.push_back({});
        result.headings.push_back({1.0F, 0.0F, 0.0F});
        result.hues.push_back(0U);
        return result;
    }
}

TEST_CASE("replicated entity id zero is rejected", "[snapshot][validation]")
{
    CHECK(simnet::validate_world_snapshot(snapshot(1U)).valid);
    auto const invalid_snapshot = simnet::validate_world_snapshot(snapshot(0U));
    CHECK_FALSE(invalid_snapshot.valid);
    CHECK(invalid_snapshot.message == "world snapshot entity id zero is reserved");

    auto update = simnet::SnapshotUpdate{};
    update.upserts.push_back(entity(1U));
    update.deletes.push_back(2U);
    CHECK(simnet::validate_client_snapshot_patch(update).valid);

    update.upserts.front().id = 0U;
    auto const invalid_upsert = simnet::validate_client_snapshot_patch(update);
    CHECK_FALSE(invalid_upsert.valid);
    CHECK(invalid_upsert.message == "client snapshot patch upsert entity id zero is reserved");

    update.upserts.front().id = 1U;
    update.deletes.front() = 0U;
    auto const invalid_delete = simnet::validate_client_snapshot_patch(update);
    CHECK_FALSE(invalid_delete.valid);
    CHECK(invalid_delete.message == "client snapshot patch delete entity id zero is reserved");

    auto invalid_reconstruction = simnet::SnapshotUpdate{};
    invalid_reconstruction.upserts.push_back(entity(0U));
    auto const baseline = snapshot(3U);
    auto reconstructed = snapshot(4U);
    reconstructed.tick = 91U;
    reconstructed.positions.front() = {4.0F, -5.0F, 6.0F};
    reconstructed.headings.front() = {0.0F, 1.0F, 0.0F};
    reconstructed.hues.front() = 213U;
    auto const reconstructed_before = reconstructed;
    auto const reconstruction
        = simnet::reconstruct_world_snapshot(&baseline, invalid_reconstruction, reconstructed);
    CHECK_FALSE(reconstruction.valid);
    CHECK(reconstructed.tick == reconstructed_before.tick);
    CHECK(reconstructed.ids == reconstructed_before.ids);
    REQUIRE(reconstructed.positions.size() == reconstructed_before.positions.size());
    CHECK(reconstructed.positions.front().x == reconstructed_before.positions.front().x);
    CHECK(reconstructed.positions.front().y == reconstructed_before.positions.front().y);
    CHECK(reconstructed.positions.front().z == reconstructed_before.positions.front().z);
    REQUIRE(reconstructed.headings.size() == reconstructed_before.headings.size());
    CHECK(reconstructed.headings.front().x == reconstructed_before.headings.front().x);
    CHECK(reconstructed.headings.front().y == reconstructed_before.headings.front().y);
    CHECK(reconstructed.headings.front().z == reconstructed_before.headings.front().z);
    CHECK(reconstructed.hues == reconstructed_before.hues);
}

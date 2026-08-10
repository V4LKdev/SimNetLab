#include <catch2/catch_test_macros.hpp>

import simnet.core;
import simnet.snapshot;

namespace
{
    [[nodiscard]] simnet::EntityState entity(simnet::EntityNetId id)
    {
        return {
            .id = id,
            .classification = simnet::EntityClassification{1U},
            .heading = {1.0F, 0.0F, 0.0F},
        };
    }

    [[nodiscard]] simnet::WorldSnapshot snapshot(simnet::EntityNetId id)
    {
        auto result = simnet::WorldSnapshot{};
        result.ids.push_back(id);
        result.classifications.push_back(simnet::EntityClassification{1U});
        result.positions.push_back({});
        result.headings.push_back({1.0F, 0.0F, 0.0F});
        result.hues.push_back(0U);
        return result;
    }
}

TEST_CASE("entity classification is a strong one-byte value", "[snapshot][validation]")
{
    STATIC_REQUIRE(sizeof(simnet::EntityClassification) == 1U);
    CHECK(simnet::EntityClassification{}.value() == 0U);
    CHECK(simnet::EntityClassification{247U}.value() == 247U);
}

TEST_CASE(
    "snapshot classification validation preserves unknown nonzero values",
    "[snapshot][validation]"
)
{
    auto value = snapshot(1U);
    value.classifications.front() = simnet::EntityClassification{247U};
    CHECK(simnet::validate_world_snapshot(value).valid);

    value.classifications.clear();
    auto const mismatched = simnet::validate_world_snapshot(value);
    CHECK_FALSE(mismatched.valid);
    CHECK(mismatched.message == "world snapshot classifications size does not match ids size");

    value = snapshot(1U);
    value.classifications.front() = simnet::EntityClassification{};
    auto const zero = simnet::validate_world_snapshot(value);
    CHECK_FALSE(zero.valid);
    CHECK(zero.message == "world snapshot classification zero is reserved");

    auto update = simnet::SnapshotUpdate{};
    update.upserts.push_back(entity(1U));
    update.upserts.front().classification = simnet::EntityClassification{247U};
    CHECK(simnet::validate_client_snapshot_patch(update).valid);
    update.upserts.front().classification = simnet::EntityClassification{};
    auto const zero_upsert = simnet::validate_client_snapshot_patch(update);
    CHECK_FALSE(zero_upsert.valid);
    CHECK(zero_upsert.message == "client snapshot patch upsert classification zero is reserved");
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
    auto const reconstruction =
        simnet::reconstruct_world_snapshot(&baseline, invalid_reconstruction, reconstructed);
    CHECK_FALSE(reconstruction.valid);
    CHECK(reconstructed.tick == reconstructed_before.tick);
    CHECK(reconstructed.ids == reconstructed_before.ids);
    CHECK(reconstructed.classifications == reconstructed_before.classifications);
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

TEST_CASE("FullReplace updates cannot carry deletes", "[snapshot][validation]")
{
    auto update = simnet::SnapshotUpdate{
        .tick = 17U,
        .kind = simnet::SnapshotKind::FullReplace,
        .upserts = {entity(1U)},
        .deletes = {},
    };
    CHECK(simnet::validate_client_snapshot_patch(update).valid);

    update.deletes.push_back(2U);
    auto const invalid = simnet::validate_client_snapshot_patch(update);
    CHECK_FALSE(invalid.valid);
    CHECK(invalid.message == "full replacement snapshot update deletes must be empty");

    auto reconstructed = snapshot(4U);
    reconstructed.tick = 91U;
    reconstructed.positions.front() = {4.0F, -5.0F, 6.0F};
    reconstructed.headings.front() = {0.0F, 1.0F, 0.0F};
    reconstructed.hues.front() = 213U;
    auto const reconstructed_before = reconstructed;
    auto const reconstruction = simnet::reconstruct_world_snapshot(nullptr, update, reconstructed);
    CHECK_FALSE(reconstruction.valid);
    CHECK(reconstruction.message == invalid.message);
    CHECK(reconstructed.tick == reconstructed_before.tick);
    CHECK(reconstructed.ids == reconstructed_before.ids);
    CHECK(reconstructed.classifications == reconstructed_before.classifications);
    REQUIRE(reconstructed.positions.size() == reconstructed_before.positions.size());
    CHECK(reconstructed.positions.front().x == reconstructed_before.positions.front().x);
    CHECK(reconstructed.positions.front().y == reconstructed_before.positions.front().y);
    CHECK(reconstructed.positions.front().z == reconstructed_before.positions.front().z);
    REQUIRE(reconstructed.headings.size() == reconstructed_before.headings.size());
    CHECK(reconstructed.headings.front().x == reconstructed_before.headings.front().x);
    CHECK(reconstructed.headings.front().y == reconstructed_before.headings.front().y);
    CHECK(reconstructed.headings.front().z == reconstructed_before.headings.front().z);
    CHECK(reconstructed.hues == reconstructed_before.hues);

    update.kind = simnet::SnapshotKind::Patch;
    CHECK(simnet::validate_client_snapshot_patch(update).valid);
}

TEST_CASE("validated Patch reconstruction still requires a baseline", "[snapshot][validation]")
{
    auto const update = simnet::SnapshotUpdate{
        .tick = 17U,
        .kind = simnet::SnapshotKind::Patch,
        .upserts = {entity(1U)},
        .deletes = {},
    };
    REQUIRE(simnet::validate_client_snapshot_patch(update).valid);
    auto reconstructed = snapshot(4U);
    reconstructed.tick = 91U;
    auto const reconstructed_before = reconstructed;

    auto const result =
        simnet::reconstruct_world_snapshot_unchecked(nullptr, update, reconstructed);
    CHECK_FALSE(result.valid);
    CHECK(result.message == "snapshot patch requires a baseline");
    CHECK(reconstructed.tick == reconstructed_before.tick);
    CHECK(reconstructed.ids == reconstructed_before.ids);
    CHECK(reconstructed.classifications == reconstructed_before.classifications);
    CHECK(reconstructed.positions.size() == reconstructed_before.positions.size());
    CHECK(reconstructed.headings.size() == reconstructed_before.headings.size());
    CHECK(reconstructed.hues == reconstructed_before.hues);
}

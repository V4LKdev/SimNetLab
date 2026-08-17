#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

import simnet.app_snapshot_delivery;
import simnet.core;
import simnet.pipeline;
import simnet.snapshot;

namespace
{
    [[nodiscard]] simnet::WorldSnapshot
    snapshot(std::initializer_list<simnet::EntityNetId> ids)
    {
        auto value = simnet::WorldSnapshot{};
        value.reserve(ids.size());
        for (auto const id : ids)
        {
            value.ids.push_back(id);
            value.classifications.push_back(simnet::EntityClassification{1U});
            value.positions.push_back({.x = static_cast<float>(id)});
            value.headings.push_back({.z = 1.0F});
            value.hues.push_back(static_cast<std::uint8_t>(id));
        }
        return value;
    }
}

TEST_CASE("ACK promotes a retained snapshot to the replication baseline", "[delivery][ack]")
{
    auto state = simnet::app::SnapshotDeliveryState{};
    auto result = snapshot({1U, 7U});
    auto const plan = simnet::app::plan_snapshot_retention(state, result);
    REQUIRE(plan.valid);

    simnet::app::commit_submitted_snapshot(
        state,
        1U,
        std::move(result),
        simnet::SnapshotKind::FullReplace,
        simnet::Nanoseconds{1U},
        plan
    );

    CHECK(state.latest_submitted_sequence == 1U);
    CHECK(state.latest_acknowledged_sequence == 0U);
    CHECK_FALSE(state.acknowledged.has_value());

    REQUIRE(
        simnet::app::promote_snapshot_ack(state, 1U, simnet::Nanoseconds{2U}) ==
        simnet::app::AckPromotionOutcome::Promoted
    );
    REQUIRE(state.acknowledged.has_value());
    CHECK(state.latest_acknowledged_sequence == 1U);
    CHECK(state.acknowledged->snapshot.ids == std::vector<simnet::EntityNetId>{1U, 7U});
    CHECK_FALSE(state.recovery_active);
}

TEST_CASE("ACK for a missing retained result activates recovery", "[delivery][ack][recovery]")
{
    auto state = simnet::app::SnapshotDeliveryState{
        .latest_submitted_sequence = 9U,
    };

    CHECK(
        simnet::app::promote_snapshot_ack(state, 8U, simnet::Nanoseconds{8U}) ==
        simnet::app::AckPromotionOutcome::Missing
    );
    CHECK(state.recovery_active);
    CHECK(state.recovery_reason == simnet::app::SnapshotRecoveryReason::MissingRetainedResult);
}

TEST_CASE("FullReplace recovery ends only after a retained FullReplace is ACKed", "[delivery][recovery]")
{
    auto state = simnet::app::SnapshotDeliveryState{};

    auto patch_result = snapshot({1U});
    auto patch_plan = simnet::app::plan_snapshot_retention(state, patch_result);
    REQUIRE(patch_plan.valid);
    simnet::app::commit_submitted_snapshot(
        state,
        1U,
        std::move(patch_result),
        simnet::SnapshotKind::Patch,
        simnet::Nanoseconds{1U},
        patch_plan
    );
    REQUIRE(
        simnet::app::promote_snapshot_ack(state, 1U, simnet::Nanoseconds{2U}) ==
        simnet::app::AckPromotionOutcome::Promoted
    );
    CHECK(state.recovery_active);

    auto full_result = snapshot({1U, 2U});
    auto full_plan = simnet::app::plan_snapshot_retention(state, full_result);
    REQUIRE(full_plan.valid);
    simnet::app::commit_submitted_snapshot(
        state,
        2U,
        std::move(full_result),
        simnet::SnapshotKind::FullReplace,
        simnet::Nanoseconds{3U},
        full_plan
    );
    REQUIRE(
        simnet::app::promote_snapshot_ack(state, 2U, simnet::Nanoseconds{4U}) ==
        simnet::app::AckPromotionOutcome::Promoted
    );
    CHECK_FALSE(state.recovery_active);
}

TEST_CASE("Delayed ACK reconciles recovery against the newest submitted result", "[delivery][ack][recovery]")
{
    auto state = simnet::app::SnapshotDeliveryState{};
    auto submit = [&](simnet::SequenceId sequence, simnet::WorldSnapshot result)
    {
        auto const plan = simnet::app::plan_snapshot_retention(state, result);
        REQUIRE(plan.valid);
        simnet::app::commit_submitted_snapshot(
            state,
            sequence,
            std::move(result),
            simnet::SnapshotKind::Patch,
            simnet::Nanoseconds{sequence},
            plan
        );
    };

    SECTION("newer spawn remains queued")
    {
        submit(1U, snapshot({1U}));
        auto newest = snapshot({1U, 8U});
        auto const spawned = simnet::app::find_entity_state(newest, 8U);
        REQUIRE(spawned.has_value());
        state.recovery_upserts.push_back(*spawned);
        submit(2U, std::move(newest));

        REQUIRE(
            simnet::app::promote_snapshot_ack(state, 1U, simnet::Nanoseconds{3U}) ==
            simnet::app::AckPromotionOutcome::Promoted
        );
        REQUIRE(state.recovery_upserts.size() == 1U);
        CHECK(simnet::app::same_entity_state(state.recovery_upserts.front(), *spawned));
    }

    SECTION("newer changed state replaces an older queued state")
    {
        auto older = snapshot({7U});
        auto const older_entity = simnet::app::find_entity_state(older, 7U);
        REQUIRE(older_entity.has_value());
        submit(1U, std::move(older));

        auto newest = snapshot({7U});
        newest.positions.front().x = 70.0F;
        auto const newest_entity = simnet::app::find_entity_state(newest, 7U);
        REQUIRE(newest_entity.has_value());
        state.recovery_upserts.push_back(*older_entity);
        submit(2U, std::move(newest));

        REQUIRE(
            simnet::app::promote_snapshot_ack(state, 1U, simnet::Nanoseconds{3U}) ==
            simnet::app::AckPromotionOutcome::Promoted
        );
        REQUIRE(state.recovery_upserts.size() == 1U);
        CHECK(simnet::app::same_entity_state(state.recovery_upserts.front(), *newest_entity));
    }

    SECTION("newest deletion removes the queued state")
    {
        auto older = snapshot({7U});
        auto const deleted_entity = simnet::app::find_entity_state(older, 7U);
        REQUIRE(deleted_entity.has_value());
        state.recovery_upserts.push_back(*deleted_entity);
        submit(1U, std::move(older));
        submit(2U, snapshot({}));

        REQUIRE(
            simnet::app::promote_snapshot_ack(state, 1U, simnet::Nanoseconds{3U}) ==
            simnet::app::AckPromotionOutcome::Promoted
        );
        CHECK(state.recovery_upserts.empty());
    }
}

TEST_CASE(
    "ACK-relative Incremental Delta recovers lost deletes and spawns",
    "[delivery][loss][pipeline]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Incremental |
                          simnet::PipelineTechniqueFlags::Delta |
                          simnet::PipelineTechniqueFlags::DeltaFieldMask;
    pipeline.incremental.max_entities_per_update = 1U;

    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto delivery = simnet::app::SnapshotDeliveryState{};
    auto recovery_ids = std::vector<simnet::EntityNetId>{};

    auto commit = [&](simnet::EncodeOutput& encoded)
    {
        auto const plan =
            simnet::app::plan_snapshot_retention(delivery, encoded.resulting_snapshot);
        REQUIRE(plan.valid);

        if (encoded.report.snapshot_kind == simnet::SnapshotKind::Patch)
        {
            REQUIRE(simnet::app::merge_recovery_upserts(delivery, scratch.logical_update));
        }

        simnet::app::commit_submitted_snapshot(
            delivery,
            encoded.update.sequence,
            std::move(encoded.resulting_snapshot),
            encoded.report.snapshot_kind,
            simnet::Nanoseconds{encoded.update.sequence},
            plan
        );
    };

    auto encode_against_ack = [&](simnet::WorldSnapshot const& current)
    {
        REQUIRE(delivery.acknowledged.has_value());

        recovery_ids.clear();
        for (auto const& upsert : delivery.recovery_upserts)
        {
            recovery_ids.push_back(upsert.id);
        }

        return simnet::encode_snapshot(
            pipeline,
            encode_state,
            scratch,
            {
                .snapshot = &current,
                .baseline_snapshot = &delivery.acknowledged->snapshot,
                .baseline_sequence = delivery.acknowledged->sequence,
                .recovery_upsert_ids = recovery_ids,
            }
        );
    };

    auto decode_and_reconstruct =
        [&](simnet::EncodeOutput const& encoded, simnet::WorldSnapshot const& baseline)
    {
        auto decoded = simnet::decode_update(
            pipeline,
            decode_state,
            {
                .bytes = encoded.update.bytes,
                .baseline_snapshot = encoded.report.snapshot_kind == simnet::SnapshotKind::Patch
                                         ? &baseline
                                         : nullptr,
                .baseline_sequence = encoded.report.baseline_sequence,
            }
        );
        REQUIRE(decoded.report.valid);

        auto reconstructed = simnet::WorldSnapshot{};
        REQUIRE(
            simnet::reconstruct_world_snapshot_unchecked(
                decoded.update.kind == simnet::SnapshotKind::Patch ? &baseline : nullptr,
                decoded.update,
                reconstructed
            )
                .valid
        );
        return reconstructed;
    };

    auto initial = snapshot({1U, 7U});
    initial.tick = 1U;
    auto seed = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {.snapshot = &initial, .force_full_replace = true}
    );
    auto client_baseline = decode_and_reconstruct(seed, initial);
    commit(seed);
    REQUIRE(
        simnet::app::promote_snapshot_ack(delivery, 1U, simnet::Nanoseconds{1U}) ==
        simnet::app::AckPromotionOutcome::Promoted
    );

    auto removed = snapshot({1U});
    removed.tick = 2U;
    auto lost_delete = encode_against_ack(removed);
    REQUIRE(lost_delete.report.delete_count == 1U);
    commit(lost_delete);

    removed.tick = 3U;
    auto repeated_delete = encode_against_ack(removed);
    REQUIRE(repeated_delete.report.delete_count == 1U);
    auto after_delete = decode_and_reconstruct(repeated_delete, client_baseline);
    CHECK(after_delete.ids == std::vector<simnet::EntityNetId>{1U});
    commit(repeated_delete);
    REQUIRE(
        simnet::app::promote_snapshot_ack(delivery, 3U, simnet::Nanoseconds{3U}) ==
        simnet::app::AckPromotionOutcome::Promoted
    );
    client_baseline = after_delete;

    auto spawned = snapshot({1U, 8U});
    spawned.tick = 4U;
    auto scheduling_before_spawn = encode_against_ack(spawned);
    commit(scheduling_before_spawn);

    spawned.tick = 5U;
    auto lost_spawn = encode_against_ack(spawned);
    REQUIRE(lost_spawn.report.upsert_count == 1U);
    REQUIRE(scratch.logical_update.upserts.front().id == 8U);
    commit(lost_spawn);
    REQUIRE(delivery.recovery_upserts.size() == 1U);

    spawned.tick = 6U;
    auto recovered_spawn = encode_against_ack(spawned);
    REQUIRE(recovered_spawn.report.upsert_count == 1U);
    auto after_spawn = decode_and_reconstruct(recovered_spawn, client_baseline);
    CHECK(after_spawn.ids == std::vector<simnet::EntityNetId>{1U, 8U});
    commit(recovered_spawn);

    REQUIRE(
        simnet::app::promote_snapshot_ack(delivery, 6U, simnet::Nanoseconds{6U}) ==
        simnet::app::AckPromotionOutcome::Promoted
    );
    CHECK(delivery.recovery_upserts.empty());
}

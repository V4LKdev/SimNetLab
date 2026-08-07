#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../app/server_peer_iteration.hpp"

import simnet.app_snapshot_delivery;
import simnet.app_protocol;
import simnet.core;
import simnet.pipeline;
import simnet.snapshot;

namespace
{
    simnet::WorldSnapshot snapshot(std::initializer_list<simnet::EntityNetId> ids)
    {
        auto value = simnet::WorldSnapshot{};
        value.reserve(ids.size());
        for (auto const id : ids) {
            value.ids.push_back(id);
            value.classifications.push_back(simnet::EntityClassification{1U});
            value.positions.push_back({.x = static_cast<float>(id)});
            value.headings.push_back({.z = 1.0F});
            value.hues.push_back(static_cast<std::uint8_t>(id));
        }
        return value;
    }
}

TEST_CASE("sorted peer iteration remains complete across erasure", "[peer][delivery]")
{
    struct Peer
    {
        simnet::PeerId id{};
        bool joined{true};
    };

    auto peers = std::vector<Peer>{{1U}, {2U}, {3U}, {4U, false}};
    auto processed = std::vector<simnet::PeerId>{};
    auto erased = std::vector<simnet::PeerId>{};
    simnet::app::detail::process_sorted_peer_states(
        peers,
        [](Peer const& peer) {
            return peer.joined;
        },
        [&](Peer const& peer) {
            processed.push_back(peer.id);
            return peer.id != 2U;
        },
        [&](Peer const& peer) {
            erased.push_back(peer.id);
        }
    );

    CHECK(processed == std::vector<simnet::PeerId>{1U, 2U, 3U});
    CHECK(erased == std::vector<simnet::PeerId>{2U});
    REQUIRE(peers.size() == 3U);
    CHECK(peers[0].id == 1U);
    CHECK(peers[1].id == 3U);
    CHECK(peers[2].id == 4U);
}

TEST_CASE("peer admission rejects overflow without touching existing state", "[peer][protocol]")
{
    struct Peer
    {
        simnet::PeerId id{};
        simnet::SequenceId next_sequence{1U};
    };

    auto const peers = std::vector<Peer>{{1U, 7U}, {2U, 11U}};
    CHECK(
        simnet::app::detail::peer_admission(peers, 3U, 2U, &Peer::id)
        == simnet::app::detail::PeerAdmission::Full
    );
    CHECK(
        simnet::app::detail::peer_admission(peers, 2U, 2U, &Peer::id)
        == simnet::app::detail::PeerAdmission::Duplicate
    );
    REQUIRE(peers.size() == 2U);
    CHECK(peers[0].id == 1U);
    CHECK(peers[0].next_sequence == 7U);
    CHECK(peers[1].id == 2U);
    CHECK(peers[1].next_sequence == 11U);
}

TEST_CASE("submitted snapshots remain distinct from the acknowledged replica", "[delivery]")
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

    CHECK(
        simnet::app::promote_snapshot_ack(state, 1U, simnet::Nanoseconds{2U})
        == simnet::app::AckPromotionOutcome::Promoted
    );
    REQUIRE(state.acknowledged.has_value());
    CHECK(state.acknowledged->snapshot.ids == std::vector<simnet::EntityNetId>{1U, 7U});
    CHECK_FALSE(state.recovery_active);
    CHECK(
        simnet::app::promote_snapshot_ack(state, 1U, simnet::Nanoseconds{3U})
        == simnet::app::AckPromotionOutcome::Duplicate
    );
}

TEST_CASE("peer ACK and recovery state remain independent", "[peer][delivery][recovery]")
{
    auto first = simnet::app::SnapshotDeliveryState{};
    auto second = simnet::app::SnapshotDeliveryState{};
    auto first_result = snapshot({1U, 2U});
    auto second_result = snapshot({7U});
    auto const first_plan = simnet::app::plan_snapshot_retention(first, first_result);
    auto const second_plan = simnet::app::plan_snapshot_retention(second, second_result);
    REQUIRE(first_plan.valid);
    REQUIRE(second_plan.valid);
    simnet::app::commit_submitted_snapshot(
        first,
        1U,
        std::move(first_result),
        simnet::SnapshotKind::FullReplace,
        simnet::Nanoseconds{1U},
        first_plan
    );
    simnet::app::commit_submitted_snapshot(
        second,
        1U,
        std::move(second_result),
        simnet::SnapshotKind::FullReplace,
        simnet::Nanoseconds{1U},
        second_plan
    );

    REQUIRE(
        simnet::app::promote_snapshot_ack(first, 1U, simnet::Nanoseconds{2U})
        == simnet::app::AckPromotionOutcome::Promoted
    );
    CHECK(first.latest_acknowledged_sequence == 1U);
    CHECK_FALSE(first.recovery_active);
    CHECK(second.latest_acknowledged_sequence == 0U);
    CHECK(second.recovery_active);
    REQUIRE(second.submitted.size() == 1U);

    auto recovery = simnet::SnapshotUpdate{
        .kind = simnet::SnapshotKind::Patch,
        .upserts = {{
            .id = 7U,
            .classification = simnet::EntityClassification{1U},
            .position = {.x = 9.0F},
            .heading = {.z = 1.0F},
            .hue = 7U,
        }},
        .deletes = {},
    };
    REQUIRE(simnet::app::merge_recovery_upserts(second, recovery));
    CHECK(second.recovery_upserts.size() == 1U);
    CHECK(first.recovery_upserts.empty());
}

TEST_CASE("reconnected peer authority starts completely fresh", "[peer][delivery][aoi][lod]")
{
    auto const former_identity = simnet::app::encode_app_message({
        .kind = simnet::app::AppMessageKind::JoinAccepted,
        .role = simnet::app::ClientRole::Player,
        .peer_id = 8U,
        .player_id = 100U,
    });
    auto const reconnected_identity = simnet::app::encode_app_message({
        .kind = simnet::app::AppMessageKind::JoinAccepted,
        .role = simnet::app::ClientRole::Player,
        .peer_id = 9U,
        .player_id = 101U,
    });
    auto former = simnet::app::AppMessage{};
    auto reconnected = simnet::app::AppMessage{};
    REQUIRE(simnet::app::decode_app_message(former_identity, former));
    REQUIRE(simnet::app::decode_app_message(reconnected_identity, reconnected));
    CHECK(reconnected.peer_id > former.peer_id);
    CHECK(reconnected.player_id > former.player_id);

    auto state = simnet::ClientReplicationState{};
    auto delivery = simnet::app::SnapshotDeliveryState{};
    auto observer = simnet::app::StationaryObserverInterestState{};
    CHECK(state.next_sequence == 1U);
    CHECK(state.incremental_cursor == 0U);
    CHECK_FALSE(state.incremental_seeded);
    CHECK_FALSE(state.level_of_detail_seeded);
    CHECK(state.level_of_detail_schedule.empty());
    CHECK(delivery.latest_submitted_sequence == 0U);
    CHECK(delivery.latest_acknowledged_sequence == 0U);
    CHECK_FALSE(delivery.acknowledged.has_value());
    CHECK(delivery.submitted.empty());
    CHECK(delivery.recovery_upserts.empty());
    CHECK(delivery.recovery_active);
    CHECK(delivery.recovery_reason == simnet::app::SnapshotRecoveryReason::NoAcknowledgedBaseline);
    CHECK(delivery.forced_full_replace_count == 0U);
    CHECK_FALSE(observer.initialized);

    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Incremental;
    pipeline.incremental.max_entities_per_update = 1U;
    pipeline.area_of_interest = {
        .mode = simnet::AreaOfInterestMode::Radius,
        .radius = 10.0F,
    };
    pipeline.level_of_detail = {
        .mode = simnet::LevelOfDetailMode::DistanceBands,
        .near_distance = 2.0F,
        .medium_distance = 5.0F,
        .medium_interval_ticks = 2U,
        .far_interval_ticks = 4U,
    };
    auto source = snapshot({1U, 2U});
    source.tick = 1U;
    auto const interest = simnet::InterestSource{
        .position = {},
        .forward = {.z = 1.0F},
    };
    auto const candidates = std::vector<std::uint32_t>{0U, 1U};
    auto scratch = simnet::PipelineScratch{};
    auto const initial = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .force_full_replace = true,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    REQUIRE(initial.kind == simnet::EncodeResultKind::Update);
    CHECK(initial.update.sequence == 1U);
    CHECK(initial.report.baseline_sequence == 0U);
    CHECK(initial.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
}

TEST_CASE("canonical recovery upserts persist until an ACK proves their result", "[delivery]")
{
    auto state = simnet::app::SnapshotDeliveryState{};
    auto update = simnet::SnapshotUpdate{
        .tick = {},
        .kind = simnet::SnapshotKind::Patch,
        .upserts = {},
        .deletes = {},
    };
    update.upserts.push_back({
        .id = 7U,
        .classification = simnet::EntityClassification{1U},
        .position = {.x = 7.0F},
        .heading = {.z = 1.0F},
        .hue = 7U,
    });
    REQUIRE(simnet::app::merge_recovery_upserts(state, update));
    REQUIRE(state.recovery_upserts.size() == 1U);

    update.upserts.front().position.x = 8.0F;
    REQUIRE(simnet::app::merge_recovery_upserts(state, update));
    CHECK(state.recovery_upserts.size() == 1U);
    CHECK(state.recovery_upserts.front().position.x == 8.0F);

    auto result = snapshot({7U});
    result.positions.front().x = 8.0F;
    auto const plan = simnet::app::plan_snapshot_retention(state, result);
    REQUIRE(plan.valid);
    simnet::app::commit_submitted_snapshot(
        state,
        4U,
        std::move(result),
        simnet::SnapshotKind::Patch,
        simnet::Nanoseconds{4U},
        plan
    );
    REQUIRE(
        simnet::app::promote_snapshot_ack(state, 4U, simnet::Nanoseconds{5U})
        == simnet::app::AckPromotionOutcome::Promoted
    );
    CHECK(state.recovery_upserts.empty());
}

TEST_CASE("missing and expired retained results enter bounded recovery", "[delivery]")
{
    auto state = simnet::app::SnapshotDeliveryState{};
    state.latest_submitted_sequence = 9U;
    CHECK(
        simnet::app::promote_snapshot_ack(state, 8U, simnet::Nanoseconds{8U})
        == simnet::app::AckPromotionOutcome::Missing
    );
    CHECK(state.recovery_active);
    CHECK(state.recovery_reason == simnet::app::SnapshotRecoveryReason::MissingRetainedResult);

    auto result = snapshot({1U});
    auto const plan = simnet::app::plan_snapshot_retention(state, result);
    REQUIRE(plan.valid);
    simnet::app::commit_submitted_snapshot(
        state,
        10U,
        std::move(result),
        simnet::SnapshotKind::FullReplace,
        simnet::Nanoseconds{},
        plan
    );
    simnet::app::expire_retained_snapshots(
        state,
        simnet::app::maximum_retained_result_age + simnet::Nanoseconds{1U}
    );
    CHECK(state.submitted.empty());
    CHECK(state.baseline_eviction_count == 1U);
}

TEST_CASE("ACK delay and retained capacity pressure trigger bounded recovery", "[delivery]")
{
    auto delayed = simnet::app::SnapshotDeliveryState{
        .submissions_since_ack_progress = 31U,
    };
    CHECK_FALSE(simnet::app::ack_progress_stalled(delayed, 32U));
    ++delayed.submissions_since_ack_progress;
    CHECK(simnet::app::ack_progress_stalled(delayed, 32U));

    auto pressured = simnet::app::SnapshotDeliveryState{};
    pressured.acknowledged = simnet::app::SubmittedSnapshotResult{
        .sequence = 1U,
        .capacity_bytes = simnet::app::maximum_retained_capacity_bytes,
    };
    pressured.latest_acknowledged_sequence = 1U;
    pressured.retained_capacity_bytes = simnet::app::maximum_retained_capacity_bytes;
    auto result = snapshot({2U});
    CHECK_FALSE(simnet::app::plan_snapshot_retention(pressured, result).valid);
    simnet::app::discard_acknowledged_replica(
        pressured,
        simnet::app::SnapshotRecoveryReason::RetentionPressure
    );
    CHECK_FALSE(pressured.acknowledged.has_value());
    CHECK(pressured.recovery_active);
    CHECK(pressured.recovery_reason == simnet::app::SnapshotRecoveryReason::RetentionPressure);
}

TEST_CASE("Client recovery requests are deduplicated until canonical progress", "[delivery]")
{
    auto state = simnet::app::ClientRecoveryRequestState{};
    simnet::app::record_missing_baseline_rejection(state);
    CHECK(simnet::app::recovery_request_needed(state, 4U));
    simnet::app::record_recovery_request(state, 4U);
    CHECK_FALSE(simnet::app::recovery_request_needed(state, 4U));
    CHECK(simnet::app::recovery_request_needed(state, 5U));
    simnet::app::record_snapshot_progress(state);
    CHECK(simnet::app::recovery_request_needed(state, 4U));
    CHECK(state.sent_count == 1U);
    CHECK(state.missing_baseline_rejection_count == 1U);
}

TEST_CASE("Server recovery requests must describe a current retained update", "[delivery]")
{
    auto state = simnet::app::SnapshotDeliveryState{
        .latest_submitted_sequence = 14U,
        .latest_acknowledged_sequence = 10U,
    };

    CHECK(simnet::app::valid_recovery_request(state, 14U, 10U));
    CHECK_FALSE(simnet::app::valid_recovery_request(state, 14U, 9U));
    CHECK_FALSE(simnet::app::valid_recovery_request(state, 10U, 9U));
    CHECK_FALSE(simnet::app::valid_recovery_request(state, 15U, 10U));
    CHECK_FALSE(simnet::app::valid_recovery_request(state, 14U, 14U));
}

TEST_CASE("FullReplace recovery persists until a retained FullReplace is ACKed", "[delivery]")
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
        simnet::app::promote_snapshot_ack(state, 1U, simnet::Nanoseconds{2U})
        == simnet::app::AckPromotionOutcome::Promoted
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
        simnet::app::promote_snapshot_ack(state, 2U, simnet::Nanoseconds{4U})
        == simnet::app::AckPromotionOutcome::Promoted
    );
    CHECK_FALSE(state.recovery_active);
}

TEST_CASE("retained result count pressure rejects an evicted ACK", "[delivery]")
{
    auto state = simnet::app::SnapshotDeliveryState{};
    auto constexpr last_sequence
        = static_cast<simnet::SequenceId>(simnet::app::maximum_retained_results + 1U);
    for (auto sequence = simnet::SequenceId{1U}; sequence <= last_sequence; ++sequence) {
        auto result = snapshot({sequence});
        auto const plan = simnet::app::plan_snapshot_retention(state, result);
        REQUIRE(plan.valid);
        simnet::app::commit_submitted_snapshot(
            state,
            sequence,
            std::move(result),
            simnet::SnapshotKind::FullReplace,
            simnet::Nanoseconds{sequence},
            plan
        );
    }
    CHECK(state.submitted.size() == simnet::app::maximum_retained_results);
    CHECK(state.submitted.front().sequence == 2U);
    CHECK(
        simnet::app::promote_snapshot_ack(state, 1U, simnet::Nanoseconds{66U})
        == simnet::app::AckPromotionOutcome::Missing
    );
    CHECK(state.recovery_reason == simnet::app::SnapshotRecoveryReason::MissingRetainedResult);
}

TEST_CASE(
    "ACK-relative Incremental Delta converges lost deletes spawns and re-entry",
    "[delivery][loss][pipeline]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques
        = simnet::PipelineTechniqueFlags::Incremental | simnet::PipelineTechniqueFlags::Delta;
    pipeline.incremental.max_entities_per_update = 1U;
    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto delivery = simnet::app::SnapshotDeliveryState{};
    auto recovery_ids = std::vector<simnet::EntityNetId>{};

    auto commit = [&](simnet::EncodeOutput& encoded) {
        auto const plan
            = simnet::app::plan_snapshot_retention(delivery, encoded.resulting_snapshot);
        REQUIRE(plan.valid);
        if (encoded.report.snapshot_kind == simnet::SnapshotKind::Patch) {
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
    auto encode_against_ack = [&](simnet::WorldSnapshot const& current) {
        REQUIRE(delivery.acknowledged.has_value());
        recovery_ids.clear();
        for (auto const& upsert : delivery.recovery_upserts) {
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
    auto decode_and_reconstruct
        = [&](simnet::EncodeOutput const& encoded, simnet::WorldSnapshot const& baseline) {
              auto decoded
                  = simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});
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
        simnet::app::promote_snapshot_ack(delivery, 1U, simnet::Nanoseconds{1U})
        == simnet::app::AckPromotionOutcome::Promoted
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
        simnet::app::promote_snapshot_ack(delivery, 3U, simnet::Nanoseconds{3U})
        == simnet::app::AckPromotionOutcome::Promoted
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
        simnet::app::promote_snapshot_ack(delivery, 6U, simnet::Nanoseconds{6U})
        == simnet::app::AckPromotionOutcome::Promoted
    );
    CHECK(delivery.recovery_upserts.empty());
    client_baseline = after_spawn;

    auto left_again = snapshot({1U});
    left_again.tick = 7U;
    auto applied_but_unacknowledged_delete = encode_against_ack(left_again);
    auto client_without_entity
        = decode_and_reconstruct(applied_but_unacknowledged_delete, client_baseline);
    REQUIRE(client_without_entity.ids == std::vector<simnet::EntityNetId>{1U});
    commit(applied_but_unacknowledged_delete);

    spawned.tick = 8U;
    auto reentry = encode_against_ack(spawned);
    auto restored = decode_and_reconstruct(reentry, client_baseline);
    CHECK(restored.ids == std::vector<simnet::EntityNetId>{1U, 8U});
    CHECK(reentry.report.baseline_sequence == 6U);
}

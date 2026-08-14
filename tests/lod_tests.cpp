#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

import simnet.app_snapshot_delivery;
import simnet.core;
import simnet.pipeline;
import simnet.snapshot;

namespace
{
    [[nodiscard]] simnet::WorldSnapshot
    make_snapshot(simnet::Tick tick, std::initializer_list<simnet::EntityState> entities)
    {
        auto result = simnet::WorldSnapshot{};
        result.tick = tick;
        result.reserve(entities.size());
        for (auto const& entity : entities)
        {
            result.ids.push_back(entity.id);
            result.classifications.push_back(entity.classification);
            result.positions.push_back(entity.position);
            result.headings.push_back(entity.heading);
            result.hues.push_back(entity.hue);
        }
        return result;
    }

    [[nodiscard]] simnet::WorldSnapshot
    linear_snapshot(simnet::Tick tick, std::uint32_t count, float spacing = 1.0F)
    {
        auto result = simnet::WorldSnapshot{};
        result.tick = tick;
        result.reserve(count);
        for (auto index = std::uint32_t{}; index < count; ++index)
        {
            result.ids.push_back(index + 1U);
            result.classifications.push_back(
                simnet::EntityClassification{static_cast<std::uint8_t>(index % 3U + 1U)}
            );
            result.positions.push_back({.x = static_cast<float>(index) * spacing});
            result.headings.push_back({.z = 1.0F});
            result.hues.push_back(static_cast<std::uint8_t>(index));
        }
        return result;
    }

    [[nodiscard]] std::vector<std::uint32_t> all_candidates(simnet::WorldSnapshot const& snapshot)
    {
        auto result = std::vector<std::uint32_t>(snapshot.size());
        std::iota(result.begin(), result.end(), 0U);
        return result;
    }

    [[nodiscard]] simnet::PipelineDefinition lod_pipeline(bool incremental = false)
    {
        auto pipeline = simnet::PipelineDefinition{};
        if (incremental)
        {
            pipeline.techniques |= simnet::PipelineTechniqueFlags::Incremental;
        }
        pipeline.area_of_interest = {
            .mode = simnet::AreaOfInterestMode::Radius,
            .radius = 20.0F,
        };
        pipeline.level_of_detail = {
            .mode = simnet::LevelOfDetailMode::DistanceBands,
            .near_distance = 4.0F,
            .medium_distance = 8.0F,
            .medium_interval_ticks = 2U,
            .far_interval_ticks = 4U,
        };
        return pipeline;
    }

    [[nodiscard]] simnet::InterestSource stationary_source()
    {
        return {.position = {}, .forward = {.z = 1.0F}};
    }

    [[nodiscard]] bool
    same_snapshot(simnet::WorldSnapshot const& left, simnet::WorldSnapshot const& right)
    {
        if (left.tick != right.tick || left.ids != right.ids ||
            left.classifications != right.classifications || left.hues != right.hues ||
            left.positions.size() != right.positions.size() ||
            left.headings.size() != right.headings.size())
        {
            return false;
        }
        for (auto index = std::size_t{}; index < left.size(); ++index)
        {
            if (left.positions[index].x != right.positions[index].x ||
                left.positions[index].y != right.positions[index].y ||
                left.positions[index].z != right.positions[index].z ||
                left.headings[index].x != right.headings[index].x ||
                left.headings[index].y != right.headings[index].y ||
                left.headings[index].z != right.headings[index].z)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::vector<simnet::EntityNetId> update_ids(simnet::SnapshotUpdate const& update)
    {
        auto result = std::vector<simnet::EntityNetId>{};
        result.reserve(update.upserts.size());
        for (auto const& entity : update.upserts)
        {
            result.push_back(entity.id);
        }
        return result;
    }
}

TEST_CASE("none level of detail preserves existing pipeline bytes and state", "[lod][none]")
{
    auto const source = linear_snapshot(1U, 4U);
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto baseline_pipeline = simnet::PipelineDefinition{};
    baseline_pipeline.area_of_interest = {
        .mode = simnet::AreaOfInterestMode::Radius,
        .radius = 20.0F,
    };
    auto none_pipeline = baseline_pipeline;
    none_pipeline.level_of_detail.mode = simnet::LevelOfDetailMode::None;
    auto baseline_state = simnet::ClientReplicationState{};
    auto none_state = simnet::ClientReplicationState{};
    auto baseline_scratch = simnet::PipelineScratch{};
    auto none_scratch = simnet::PipelineScratch{};

    auto const baseline = simnet::encode_snapshot(
        baseline_pipeline,
        baseline_state,
        baseline_scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );
    auto const none = simnet::encode_snapshot(
        none_pipeline,
        none_state,
        none_scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );

    CHECK(none.update.bytes == baseline.update.bytes);
    CHECK(same_snapshot(none.resulting_snapshot, baseline.resulting_snapshot));
    CHECK(none_state.next_sequence == baseline_state.next_sequence);
    CHECK(none_state.incremental_cursor == baseline_state.incremental_cursor);
    CHECK_FALSE(none_state.level_of_detail_seeded);
    CHECK(none_state.level_of_detail_schedule.empty());
}

TEST_CASE(
    "two peer pipeline states keep AOI LOD cursor and candidate commits independent",
    "[peer][pipeline][aoi][lod][incremental]"
)
{
    auto pipeline = lod_pipeline(true);
    pipeline.incremental.max_entities_per_update = 1U;
    pipeline.area_of_interest.radius = 5.0F;
    pipeline.level_of_detail.near_distance = 2.0F;
    pipeline.level_of_detail.medium_distance = 4.0F;
    auto const source = linear_snapshot(1U, 12U);
    auto const candidates = all_candidates(source);
    auto const first_interest = simnet::InterestSource{.position = {}};
    auto const second_interest = simnet::InterestSource{.position = {.x = 10.0F}};
    auto first_state = simnet::ClientReplicationState{};
    auto second_state = simnet::ClientReplicationState{};
    auto first_scratch = simnet::PipelineScratch{};
    auto second_scratch = simnet::PipelineScratch{};

    auto const first_initial = simnet::encode_snapshot(
        pipeline,
        first_state,
        first_scratch,
        {
            .snapshot = &source,
            .interest_source = &first_interest,
            .candidate_indices = candidates,
        }
    );
    auto const second_initial = simnet::encode_snapshot(
        pipeline,
        second_state,
        second_scratch,
        {
            .snapshot = &source,
            .interest_source = &second_interest,
            .candidate_indices = candidates,
        }
    );
    REQUIRE(first_initial.update.sequence == 1U);
    REQUIRE(second_initial.update.sequence == 1U);
    CHECK(first_initial.resulting_snapshot.ids != second_initial.resulting_snapshot.ids);
    CHECK(first_initial.resulting_snapshot.size() == 6U);
    CHECK(second_initial.resulting_snapshot.size() == 7U);
    CHECK(
        first_state.level_of_detail_schedule.size() != second_state.level_of_detail_schedule.size()
    );

    auto next = linear_snapshot(2U, 12U);
    auto abandoned_first = first_state;
    auto const first_candidate = simnet::encode_snapshot(
        pipeline,
        abandoned_first,
        first_scratch,
        {
            .snapshot = &next,
            .baseline_snapshot = &first_initial.resulting_snapshot,
            .baseline_sequence = first_initial.update.sequence,
            .interest_source = &first_interest,
            .candidate_indices = candidates,
        }
    );
    REQUIRE(first_candidate.kind == simnet::EncodeResultKind::Update);
    CHECK(first_candidate.update.sequence == 2U);
    CHECK(first_state.next_sequence == 2U);
    CHECK(first_state.incremental_cursor == 0U);

    auto const second_committed = simnet::encode_snapshot(
        pipeline,
        second_state,
        second_scratch,
        {
            .snapshot = &next,
            .baseline_snapshot = &second_initial.resulting_snapshot,
            .baseline_sequence = second_initial.update.sequence,
            .interest_source = &second_interest,
            .candidate_indices = candidates,
        }
    );
    REQUIRE(second_committed.kind == simnet::EncodeResultKind::Update);
    CHECK(second_committed.update.sequence == 2U);
    CHECK(second_state.next_sequence == 3U);
    CHECK(second_state.incremental_cursor != first_state.incremental_cursor);
    CHECK(
        first_state.level_of_detail_schedule.size() != second_state.level_of_detail_schedule.size()
    );
}

TEST_CASE(
    "distance LOD classifies inclusive bands and preserves classifications",
    "[lod][classification]"
)
{
    auto const source = make_snapshot(
        0U,
        {
            {.id = 1U,
             .classification = simnet::EntityClassification{2U},
             .position = {19.0F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 2U,
             .classification = simnet::EntityClassification{1U},
             .position = {4.0F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 3U,
             .classification = simnet::EntityClassification{247U},
             .position = {4.01F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 4U,
             .classification = simnet::EntityClassification{3U},
             .position = {8.0F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 5U,
             .classification = simnet::EntityClassification{1U},
             .position = {8.01F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
        }
    );
    auto const candidates = all_candidates(source);
    auto interest = stationary_source();
    interest.source_entity_id = 1U;
    auto pipeline = lod_pipeline();
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const encoded = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );

    CHECK(encoded.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    CHECK(encoded.report.level_of_detail.population.near == 2U);
    CHECK(encoded.report.level_of_detail.population.medium == 2U);
    CHECK(encoded.report.level_of_detail.population.far == 1U);
    CHECK(encoded.resulting_snapshot.classifications == source.classifications);
    REQUIRE(state.level_of_detail_schedule.size() == source.size());
    CHECK(state.level_of_detail_schedule[0].band == simnet::LevelOfDetailBand::Near);
    CHECK(encoded.report.level_of_detail.pending_due_count == 0U);
}

TEST_CASE("standalone distance LOD emits explicit-baseline Patches", "[lod][patch]")
{
    auto source = linear_snapshot(0U, 6U, 2.0F);
    auto candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto pipeline = lod_pipeline();
    auto decode_equivalent = pipeline;
    decode_equivalent.level_of_detail = {};
    CHECK(
        simnet::pipeline_decode_signature(pipeline) ==
        simnet::pipeline_decode_signature(decode_equivalent)
    );
    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto full = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );
    REQUIRE(full.report.snapshot_kind == simnet::SnapshotKind::FullReplace);

    source.tick = 1U;
    for (auto& position : source.positions)
    {
        position.y += 1.0F;
    }
    auto patch = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    REQUIRE(patch.report.snapshot_kind == simnet::SnapshotKind::Patch);
    CHECK(patch.report.baseline_sequence == full.update.sequence);
    CHECK(patch.report.upsert_count < source.size());
    auto const& lod = patch.report.level_of_detail;
    CHECK(
        lod.population.near + lod.population.medium + lod.population.far ==
        patch.report.area_of_interest.retained_count
    );
    CHECK(lod.serviced.near + lod.deferred.near == lod.eligible.near);
    CHECK(lod.serviced.medium + lod.deferred.medium == lod.eligible.medium);
    CHECK(lod.serviced.far + lod.deferred.far == lod.eligible.far);
    CHECK(
        lod.represented.near + lod.represented.medium + lod.represented.far <=
        lod.serviced.near + lod.serviced.medium + lod.serviced.far + lod.recovery_forced_count
    );

    auto const decoded_full =
        simnet::decode_update(pipeline, decode_state, {.bytes = full.update.bytes});
    REQUIRE(decoded_full.report.valid);
    auto const decoded_patch =
        simnet::decode_update(pipeline, decode_state, {.bytes = patch.update.bytes});
    REQUIRE(decoded_patch.report.valid);
    auto reconstructed = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot(
            &full.resulting_snapshot,
            decoded_patch.update,
            reconstructed
        )
            .valid
    );
    CHECK(same_snapshot(reconstructed, patch.resulting_snapshot));

    auto malformed = patch.update.bytes;
    constexpr auto baseline_sequence_offset = std::size_t{29U};
    for (auto offset = std::size_t{}; offset < 4U; ++offset)
    {
        malformed[baseline_sequence_offset + offset] = simnet::Byte{};
    }
    auto malformed_state = simnet::ClientReplicationState{};
    REQUIRE(
        simnet::decode_update(pipeline, malformed_state, {.bytes = full.update.bytes}).report.valid
    );
    CHECK_FALSE(
        simnet::decode_update(pipeline, malformed_state, {.bytes = malformed}).report.valid
    );
}

TEST_CASE("distance LOD phases are stable and distributed", "[lod][phase][determinism]")
{
    auto source = linear_snapshot(0U, 32U);
    for (auto& position : source.positions)
    {
        position.x += 10.0F;
    }
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto pipeline = lod_pipeline();
    auto first_state = simnet::ClientReplicationState{};
    auto second_state = simnet::ClientReplicationState{};
    auto first_scratch = simnet::PipelineScratch{};
    auto second_scratch = simnet::PipelineScratch{};
    static_cast<void>(simnet::encode_snapshot(
        pipeline,
        first_state,
        first_scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    ));
    static_cast<void>(simnet::encode_snapshot(
        pipeline,
        second_state,
        second_scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    ));

    REQUIRE(first_state.level_of_detail_schedule.size() == 11U);
    REQUIRE(
        first_state.level_of_detail_schedule.size() == second_state.level_of_detail_schedule.size()
    );
    auto phases = std::array<bool, 4>{};
    for (auto index = std::size_t{}; index < first_state.level_of_detail_schedule.size(); ++index)
    {
        auto const& first = first_state.level_of_detail_schedule[index];
        auto const& second = second_state.level_of_detail_schedule[index];
        CHECK(first.id == second.id);
        CHECK(first.next_due_tick == second.next_due_tick);
        if (first.band == simnet::LevelOfDetailBand::Far)
        {
            phases[first.next_due_tick % 4U] = true;
        }
    }
    CHECK(std::ranges::count(phases, true) > 1);
}

TEST_CASE("Incremental LOD drains persistent Near work without starvation", "[lod][incremental]")
{
    auto source = linear_snapshot(0U, 6U, 0.5F);
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto pipeline = lod_pipeline(true);
    pipeline.incremental.max_entities_per_update = 2U;
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto baseline = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );
    auto represented = std::vector<simnet::EntityNetId>{};
    for (simnet::Tick tick = 1U; tick <= 3U; ++tick)
    {
        source.tick = tick;
        auto encoded = simnet::encode_snapshot(
            pipeline,
            state,
            scratch,
            {
                .snapshot = &source,
                .baseline_snapshot = &baseline.resulting_snapshot,
                .baseline_sequence = baseline.update.sequence,
                .interest_source = &interest,
                .candidate_indices = candidates,
            }
        );
        for (auto const& entity : scratch.logical_update.upserts)
        {
            represented.push_back(entity.id);
        }
        baseline = std::move(encoded);
    }
    std::ranges::sort(represented);
    auto const unique = std::ranges::unique(represented).begin();
    represented.erase(unique, represented.end());
    CHECK(represented == source.ids);
}

TEST_CASE("Player self bypasses the ordinary Incremental LOD cap", "[lod][player][incremental]")
{
    auto source = linear_snapshot(0U, 3U, 1.0F);
    source.positions[2].x = 10.0F;
    auto const candidates = all_candidates(source);
    auto interest = stationary_source();
    interest.source_entity_id = 3U;
    auto pipeline = lod_pipeline(true);
    pipeline.incremental.max_entities_per_update = 1U;
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto full = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );
    source.tick = 1U;
    auto patch = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    CHECK(patch.report.upsert_count == 2U);
    CHECK(update_ids(scratch.logical_update) == std::vector<simnet::EntityNetId>{1U, 3U});
    CHECK(state.level_of_detail_schedule[2].band == simnet::LevelOfDetailBand::Near);
}

TEST_CASE("cadence preserves the conservative LOD scheduling-age bound", "[lod][cadence]")
{
    auto source = linear_snapshot(0U, 6U, 0.5F);
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto pipeline = lod_pipeline(true);
    pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval;
    pipeline.send_interval.interval_ticks = 3U;
    pipeline.incremental.max_entities_per_update = 2U;
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto baseline = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );
    auto represented = std::vector<simnet::EntityNetId>{};
    for (simnet::Tick tick = 1U; tick <= 9U; ++tick)
    {
        source.tick = tick;
        auto encoded = simnet::encode_snapshot(
            pipeline,
            state,
            scratch,
            {
                .snapshot = &source,
                .baseline_snapshot = &baseline.resulting_snapshot,
                .baseline_sequence = baseline.update.sequence,
                .interest_source = &interest,
                .candidate_indices = candidates,
            }
        );
        if (encoded.kind == simnet::EncodeResultKind::Skipped)
        {
            continue;
        }
        for (auto const& entity : scratch.logical_update.upserts)
        {
            represented.push_back(entity.id);
        }
        baseline = std::move(encoded);
    }
    std::ranges::sort(represented);
    auto const unique = std::ranges::unique(represented).begin();
    represented.erase(unique, represented.end());
    CHECK(represented == source.ids);
    CHECK(9U <= 1U + 3U * 3U);
}

TEST_CASE(
    "LOD lifecycle changes remain immediate and transactionally scheduled",
    "[lod][lifecycle]"
)
{
    auto source = linear_snapshot(0U, 2U, 10.0F);
    auto candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto pipeline = lod_pipeline();
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto baseline = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );

    source.tick = 1U;
    source.ids.pop_back();
    source.classifications.pop_back();
    source.positions.pop_back();
    source.headings.pop_back();
    source.hues.pop_back();
    candidates = all_candidates(source);
    auto left = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &baseline.resulting_snapshot,
            .baseline_sequence = baseline.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    CHECK(left.report.delete_count == 1U);
    CHECK(left.report.level_of_detail.deletions_bypassing_count == 1U);
    CHECK(state.level_of_detail_schedule.size() == 1U);
    baseline = std::move(left);

    source.tick = 2U;
    source.ids.push_back(2U);
    source.classifications.push_back(simnet::EntityClassification{247U});
    source.positions.push_back({10.0F, 0.0F, 0.0F});
    source.headings.push_back({.z = 1.0F});
    source.hues.push_back(2U);
    candidates = all_candidates(source);
    auto reentry = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &baseline.resulting_snapshot,
            .baseline_sequence = baseline.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    CHECK(update_ids(scratch.logical_update) == std::vector<simnet::EntityNetId>{2U});
    CHECK(reentry.report.level_of_detail.forced_immediate_count == 1U);
    baseline = std::move(reentry);

    source.tick = 3U;
    source.positions[1].x = 2.0F;
    auto inward = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &baseline.resulting_snapshot,
            .baseline_sequence = baseline.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    CHECK(inward.report.level_of_detail.transition_count == 1U);
    auto const inward_ids = update_ids(scratch.logical_update);
    CHECK(std::ranges::find(inward_ids, 2U) != inward_ids.end());
    baseline = std::move(inward);

    source.tick = 4U;
    source.positions[1].x = 10.0F;
    auto outward = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &baseline.resulting_snapshot,
            .baseline_sequence = baseline.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    CHECK(outward.report.level_of_detail.transition_count == 1U);
    auto const outward_ids = update_ids(scratch.logical_update);
    CHECK(std::ranges::find(outward_ids, 2U) != outward_ids.end());

    auto const cursor = state.incremental_cursor;
    source.tick = 5U;
    auto recovery = simnet::encode_snapshot(
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
    CHECK(recovery.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    CHECK(recovery.report.upsert_count == source.size());
    CHECK(recovery.report.level_of_detail.pending_due_count == 0U);
    CHECK(state.incremental_cursor == cursor);
}

TEST_CASE("LOD state survives checked due-tick overflow", "[lod][transaction]")
{
    auto source = linear_snapshot(0U, 1U);
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto pipeline = lod_pipeline();
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto full = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );
    auto const before = state;
    source.tick = std::numeric_limits<simnet::Tick>::max();
    CHECK_THROWS(
        simnet::encode_snapshot(
            pipeline,
            state,
            scratch,
            {
                .snapshot = &source,
                .baseline_snapshot = &full.resulting_snapshot,
                .baseline_sequence = full.update.sequence,
                .interest_source = &interest,
                .candidate_indices = candidates,
            }
        )
    );
    CHECK(state.next_sequence == before.next_sequence);
    CHECK(state.incremental_cursor == before.incremental_cursor);
    CHECK(
        state.level_of_detail_schedule[0].next_due_tick ==
        before.level_of_detail_schedule[0].next_due_tick
    );
}

TEST_CASE(
    "downstream preparation or send failure leaves LOD scheduling uncommitted",
    "[lod][transaction][packetization][compression]"
)
{
    auto source = linear_snapshot(0U, 1U, 10.0F);
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto const pipeline = lod_pipeline();
    auto live_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto full = simnet::encode_snapshot(
        pipeline,
        live_state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );
    auto const due_tick = live_state.level_of_detail_schedule[0].next_due_tick;
    source.tick = due_tick;
    source.positions[0].y = 2.0F;

    auto candidate_state = live_state;
    auto candidate_scratch = simnet::PipelineScratch{};
    auto candidate = simnet::encode_snapshot(
        pipeline,
        candidate_state,
        candidate_scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    REQUIRE(candidate.report.upsert_count == 1U);
    CHECK(candidate_state.level_of_detail_schedule[0].next_due_tick > due_tick);
    CHECK(live_state.level_of_detail_schedule[0].next_due_tick == due_tick);

    auto retry_scratch = simnet::PipelineScratch{};
    auto retry = simnet::encode_snapshot(
        pipeline,
        live_state,
        retry_scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    CHECK(retry.update.sequence == candidate.update.sequence);
    CHECK(update_ids(retry_scratch.logical_update) == std::vector<simnet::EntityNetId>{1U});
    CHECK(same_snapshot(retry.resulting_snapshot, candidate.resulting_snapshot));
}

TEST_CASE(
    "LOD composes with Delta quantization octahedral heading and bit packing",
    "[lod][delta][quantized][bitpacked]"
)
{
    auto source = linear_snapshot(0U, 6U, 2.0F);
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto pipeline = lod_pipeline(true);
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::DeltaFieldMask;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Quantization;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::OctHeading;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::BitPacking;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(20.0F);
    pipeline.incremental.max_entities_per_update = 2U;
    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto full = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );
    auto decoded_full = simnet::decode_update(pipeline, decode_state, {.bytes = full.update.bytes});
    REQUIRE(decoded_full.report.valid);

    source.tick = 1U;
    source.positions[0].y = 1.25F;
    source.positions[5].y = 2.5F;
    auto patch = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    auto decoded_patch = simnet::decode_update(
        pipeline,
        decode_state,
        {
            .bytes = patch.update.bytes,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(decoded_patch.report.valid);
    auto reconstructed = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(
            &full.resulting_snapshot,
            decoded_patch.update,
            reconstructed
        )
            .valid
    );
    CHECK(same_snapshot(reconstructed, patch.resulting_snapshot));
}

TEST_CASE(
    "LOD services a due candidate whose canonical Delta value is unchanged",
    "[lod][delta][quantized]"
)
{
    auto source = linear_snapshot(0U, 1U, 2.0F);
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto pipeline = lod_pipeline();
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Quantization;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(20.0F);
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const full = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );

    source.tick = state.level_of_detail_schedule[0].next_due_tick;
    source.positions[0].y = 0.0001F;
    auto const patch = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );

    CHECK(patch.report.delta.candidate_count == 1U);
    CHECK(patch.report.delta.unchanged_count == 1U);
    CHECK(patch.report.upsert_count == 0U);
    CHECK(patch.report.level_of_detail.serviced.near == 1U);
    CHECK(patch.report.level_of_detail.represented.near == 0U);
    CHECK_FALSE(state.level_of_detail_schedule[0].pending_due);
    CHECK(state.level_of_detail_schedule[0].next_due_tick > source.tick);
    REQUIRE(patch.resulting_snapshot.positions.size() == 1U);
    CHECK(patch.resulting_snapshot.positions[0].x == full.resulting_snapshot.positions[0].x);
    CHECK(patch.resulting_snapshot.positions[0].y == full.resulting_snapshot.positions[0].y);
    CHECK(patch.resulting_snapshot.positions[0].z == full.resulting_snapshot.positions[0].z);
}

TEST_CASE("distance LOD composes with FOV AOI", "[lod][aoi][fov]")
{
    auto source = make_snapshot(
        0U,
        {
            {.id = 1U,
             .classification = simnet::EntityClassification{1U},
             .position = {0.0F, 0.0F, 2.0F},
             .heading = {.z = 1.0F}},
            {.id = 2U,
             .classification = simnet::EntityClassification{2U},
             .position = {0.0F, 0.0F, -2.0F},
             .heading = {.z = 1.0F}},
        }
    );
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto pipeline = lod_pipeline();
    pipeline.area_of_interest.mode = simnet::AreaOfInterestMode::Fov;
    pipeline.area_of_interest.fov_degrees = 90.0F;
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const encoded = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );
    CHECK(encoded.resulting_snapshot.ids == std::vector<simnet::EntityNetId>{1U});
    CHECK(encoded.report.level_of_detail.population.near == 1U);
}

TEST_CASE("ACK-relative recovery repeats a lost standalone LOD upsert", "[lod][delivery][loss]")
{
    auto source = linear_snapshot(0U, 2U, 10.0F);
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto pipeline = lod_pipeline();
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto delivery = simnet::app::SnapshotDeliveryState{};
    auto full = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );
    auto full_plan = simnet::app::plan_snapshot_retention(delivery, full.resulting_snapshot);
    REQUIRE(full_plan.valid);
    simnet::app::commit_submitted_snapshot(
        delivery,
        full.update.sequence,
        std::move(full.resulting_snapshot),
        simnet::SnapshotKind::FullReplace,
        simnet::Nanoseconds{1U},
        full_plan
    );
    REQUIRE(
        simnet::app::promote_snapshot_ack(delivery, 1U, simnet::Nanoseconds{2U}) ==
        simnet::app::AckPromotionOutcome::Promoted
    );
    REQUIRE(delivery.acknowledged.has_value());

    auto const far_due = encode_state.level_of_detail_schedule[1].next_due_tick;
    source.tick = far_due;
    source.positions[1].y = 3.0F;
    auto lost = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &delivery.acknowledged->snapshot,
            .baseline_sequence = delivery.acknowledged->sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    REQUIRE(update_ids(scratch.logical_update) == std::vector<simnet::EntityNetId>{2U});
    REQUIRE(simnet::app::merge_recovery_upserts(delivery, scratch.logical_update));
    auto lost_plan = simnet::app::plan_snapshot_retention(delivery, lost.resulting_snapshot);
    REQUIRE(lost_plan.valid);
    simnet::app::commit_submitted_snapshot(
        delivery,
        lost.update.sequence,
        std::move(lost.resulting_snapshot),
        simnet::SnapshotKind::Patch,
        simnet::Nanoseconds{3U},
        lost_plan
    );
    REQUIRE(delivery.recovery_upserts.size() == 1U);

    auto const recovery_ids = std::array<simnet::EntityNetId, 1>{2U};
    auto const nominal_due_after_attempt = encode_state.level_of_detail_schedule[1].next_due_tick;
    ++source.tick;
    auto repeated = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &delivery.acknowledged->snapshot,
            .baseline_sequence = delivery.acknowledged->sequence,
            .recovery_upsert_ids = recovery_ids,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    CHECK(repeated.report.level_of_detail.recovery_forced_count == 1U);
    CHECK(encode_state.level_of_detail_schedule[1].next_due_tick == nominal_due_after_attempt);
    CHECK(update_ids(scratch.logical_update) == std::vector<simnet::EntityNetId>{2U});
    auto decoded = simnet::decode_update(pipeline, decode_state, {.bytes = repeated.update.bytes});
    REQUIRE(decoded.report.valid);
    auto reconstructed = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(
            &delivery.acknowledged->snapshot,
            decoded.update,
            reconstructed
        )
            .valid
    );
    CHECK(same_snapshot(reconstructed, repeated.resulting_snapshot));
    auto repeated_plan =
        simnet::app::plan_snapshot_retention(delivery, repeated.resulting_snapshot);
    REQUIRE(repeated_plan.valid);
    simnet::app::commit_submitted_snapshot(
        delivery,
        repeated.update.sequence,
        std::move(repeated.resulting_snapshot),
        simnet::SnapshotKind::Patch,
        simnet::Nanoseconds{4U},
        repeated_plan
    );
    REQUIRE(
        simnet::app::promote_snapshot_ack(
            delivery,
            repeated.update.sequence,
            simnet::Nanoseconds{5U}
        ) == simnet::app::AckPromotionOutcome::Promoted
    );
    CHECK(delivery.recovery_upserts.empty());
}

TEST_CASE(
    "interpolation retains a deferred canonical entity without Client LOD",
    "[lod][interpolation]"
)
{
    auto source = make_snapshot(
        0U,
        {{.id = 1U,
          .classification = simnet::EntityClassification{1U},
          .position = {10.0F, 0.0F, 0.0F},
          .heading = {.z = 1.0F}}}
    );
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto pipeline = lod_pipeline();
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto full = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );
    source.tick = state.level_of_detail_schedule[0].next_due_tick;
    source.positions[0].y = 4.0F;
    auto represented = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    REQUIRE(represented.report.upsert_count == 1U);
    ++source.tick;
    source.positions[0].y = 8.0F;
    auto deferred = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &represented.resulting_snapshot,
            .baseline_sequence = represented.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    REQUIRE(deferred.report.upsert_count == 0U);
    CHECK(deferred.resulting_snapshot.positions[0].y == 4.0F);

    auto presentation = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::interpolate_world_snapshots(
            represented.resulting_snapshot,
            deferred.resulting_snapshot,
            0.5,
            presentation
        )
            .valid
    );
    CHECK(presentation.positions[0].y == 4.0F);
}

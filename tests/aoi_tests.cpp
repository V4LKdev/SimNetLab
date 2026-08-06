#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <initializer_list>
#include <vector>

import simnet.core;
import simnet.pipeline;
import simnet.snapshot;
import simnet.spatial;

namespace
{
    [[nodiscard]] simnet::WorldSnapshot
    make_snapshot(simnet::Tick tick, std::initializer_list<simnet::EntityState> entities)
    {
        auto snapshot = simnet::WorldSnapshot{};
        snapshot.tick = tick;
        snapshot.reserve(entities.size());
        for (auto const& entity : entities) {
            snapshot.ids.push_back(entity.id);
            snapshot.classifications.push_back(entity.classification);
            snapshot.positions.push_back(entity.position);
            snapshot.headings.push_back(entity.heading);
            snapshot.hues.push_back(entity.hue);
        }
        return snapshot;
    }

    [[nodiscard]] std::vector<std::uint32_t> query_candidates(
        simnet::WorldSnapshot const& snapshot,
        simnet::Vec3f position,
        float radius,
        bool* was_unsorted = nullptr
    )
    {
        auto grid = simnet::SpatialGrid{};
        auto scratch = simnet::SpatialGridScratch{};
        simnet::resize_spatial_grid(
            grid,
            simnet::make_spatial_grid_settings(simnet::make_centered_bounds(20.0F), 4.0F)
        );
        simnet::prepare_spatial_grid_scratch(scratch, snapshot.size(), 1U);
        simnet::build_spatial_grid_serial(grid, scratch, snapshot.positions, snapshot.ids);
        auto candidates = std::vector<std::uint32_t>{};
        static_cast<void>(simnet::query_radius(
            grid,
            snapshot.positions,
            position,
            radius,
            [&](std::uint32_t index) {
                candidates.push_back(index);
            }
        ));
        if (was_unsorted != nullptr) {
            *was_unsorted = !std::ranges::is_sorted(candidates);
        }
        std::ranges::sort(candidates);
        return candidates;
    }

    [[nodiscard]] std::vector<simnet::EntityNetId>
    decoded_ids(simnet::PipelineDefinition const& pipeline, simnet::EncodeOutput const& encoded)
    {
        auto state = simnet::ClientReplicationState{};
        auto const decoded
            = simnet::decode_update(pipeline, state, {.bytes = encoded.update.bytes});
        REQUIRE(decoded.report.valid);
        auto ids = std::vector<simnet::EntityNetId>{};
        for (auto const& entity : decoded.update.upserts) {
            ids.push_back(entity.id);
        }
        return ids;
    }
}

TEST_CASE("radius AOI is inclusive ordered and classification independent", "[aoi][radius]")
{
    auto const snapshot = make_snapshot(
        1U,
        {
            {.id = 1U,
             .classification = simnet::EntityClassification{2U},
             .position = {},
             .heading = {.z = 1.0F}},
            {.id = 2U,
             .classification = simnet::EntityClassification{1U},
             .position = {-4.0F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 3U,
             .classification = simnet::EntityClassification{247U},
             .position = {4.0F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 4U,
             .classification = simnet::EntityClassification{1U},
             .position = {4.01F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
        }
    );
    auto const source = simnet::InterestSource{
        .position = {},
        .forward = {.z = 1.0F},
        .source_entity_id = 1U,
    };
    auto was_unsorted = false;
    auto candidates = query_candidates(snapshot, source.position, 4.0F, &was_unsorted);
    CHECK(was_unsorted);
    candidates.erase(candidates.begin());
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.area_of_interest = {
        .mode = simnet::AreaOfInterestMode::Radius,
        .radius = 4.0F,
    };
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const encoded = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &snapshot,
            .interest_source = &source,
            .candidate_indices = candidates,
        }
    );

    CHECK(decoded_ids(pipeline, encoded) == std::vector<simnet::EntityNetId>{1U, 2U, 3U});
    CHECK(encoded.resulting_snapshot.classifications[0] == simnet::EntityClassification{2U});
    CHECK(encoded.resulting_snapshot.classifications[1] == simnet::EntityClassification{1U});
    CHECK(encoded.resulting_snapshot.classifications[2] == simnet::EntityClassification{247U});
    CHECK(encoded.report.area_of_interest.source_entity_count == 4U);
    CHECK(encoded.report.area_of_interest.candidate_count == 2U);
    CHECK(encoded.report.area_of_interest.retained_count == 3U);
    CHECK(encoded.report.area_of_interest.culled_count == 1U);
}

TEST_CASE("conical FOV has inclusive deterministic 3D boundaries", "[aoi][fov]")
{
    auto const snapshot = make_snapshot(
        2U,
        {
            {.id = 1U,
             .classification = simnet::EntityClassification{1U},
             .position = {},
             .heading = {.z = 1.0F}},
            {.id = 2U,
             .classification = simnet::EntityClassification{1U},
             .position = {0.0F, 0.0F, 10.0F},
             .heading = {.z = 1.0F}},
            {.id = 3U,
             .classification = simnet::EntityClassification{1U},
             .position = {1.7320508F, 0.0F, 1.0F},
             .heading = {.z = 1.0F}},
            {.id = 4U,
             .classification = simnet::EntityClassification{1U},
             .position = {1.8F, 0.0F, 1.0F},
             .heading = {.z = 1.0F}},
            {.id = 5U,
             .classification = simnet::EntityClassification{1U},
             .position = {0.0F, 0.0F, -1.0F},
             .heading = {.z = 1.0F}},
            {.id = 6U,
             .classification = simnet::EntityClassification{1U},
             .position = {0.0F, 5.0F, 5.0F},
             .heading = {.z = 1.0F}},
            {.id = 7U,
             .classification = simnet::EntityClassification{1U},
             .position = {0.0F, 0.0F, 10.01F},
             .heading = {.z = 1.0F}},
        }
    );
    auto const source = simnet::InterestSource{
        .position = {},
        .forward = {.z = 1.0F},
        .source_entity_id = 1U,
    };
    auto const candidates = query_candidates(snapshot, source.position, 10.0F);
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.area_of_interest = {
        .mode = simnet::AreaOfInterestMode::Fov,
        .radius = 10.0F,
        .fov_degrees = 120.0F,
    };
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const encoded = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &snapshot,
            .interest_source = &source,
            .candidate_indices = candidates,
        }
    );

    CHECK(decoded_ids(pipeline, encoded) == std::vector<simnet::EntityNetId>{1U, 2U, 3U, 6U});

    pipeline.area_of_interest.fov_degrees = 180.0F;
    auto perpendicular = snapshot;
    perpendicular.positions[1] = {10.0F, 0.0F, 0.0F};
    auto const hemisphere_candidates = query_candidates(perpendicular, source.position, 10.0F);
    auto const hemisphere = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &perpendicular,
            .interest_source = &source,
            .candidate_indices = hemisphere_candidates,
        }
    );
    CHECK(std::ranges::binary_search(decoded_ids(pipeline, hemisphere), 2U));
}

TEST_CASE("none AOI is equivalent to the complete source population", "[aoi][none]")
{
    auto const snapshot = make_snapshot(
        3U,
        {
            {.id = 1U,
             .classification = simnet::EntityClassification{1U},
             .position = {-10.0F, 0.0F, 0.0F},
             .heading = {.x = 1.0F}},
            {.id = 2U,
             .classification = simnet::EntityClassification{2U},
             .position = {10.0F, 0.0F, 0.0F},
             .heading = {.x = 1.0F}},
        }
    );
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const encoded = simnet::encode_snapshot(
        simnet::PipelineDefinition{},
        state,
        scratch,
        {.snapshot = &snapshot}
    );
    CHECK(encoded.resulting_snapshot.ids == snapshot.ids);
    CHECK(encoded.resulting_snapshot.positions[0].x == snapshot.positions[0].x);
    CHECK(encoded.report.area_of_interest.retained_count == snapshot.size());
}

TEST_CASE("missing AOI source skips without mutating scheduling or scratch", "[aoi][cadence]")
{
    auto const snapshot = make_snapshot(
        4U,
        {{.id = 1U,
          .classification = simnet::EntityClassification{1U},
          .position = {},
          .heading = {.z = 1.0F}}}
    );
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Incremental;
    pipeline.area_of_interest = {
        .mode = simnet::AreaOfInterestMode::Radius,
        .radius = 5.0F,
    };
    auto state = simnet::ClientReplicationState{.next_sequence = 7U, .incremental_cursor = 3U};
    auto scratch = simnet::PipelineScratch{};
    scratch.selected_indices = {9U};
    scratch.selected_delete_ids = {8U};
    scratch.bytes = {simnet::Byte{7U}};
    auto const skipped = simnet::encode_snapshot(pipeline, state, scratch, {.snapshot = &snapshot});

    CHECK(skipped.kind == simnet::EncodeResultKind::Skipped);
    CHECK(skipped.report.skip_reason == simnet::EncodeSkipReason::InterestSourceUnavailable);
    CHECK_FALSE(skipped.report.area_of_interest.source_available);
    CHECK(state.next_sequence == 7U);
    CHECK(state.incremental_cursor == 3U);
    CHECK_FALSE(state.incremental_seeded);
    CHECK(scratch.selected_indices == std::vector<std::uint32_t>{9U});
    CHECK(scratch.selected_delete_ids == std::vector<simnet::EntityNetId>{8U});
    CHECK(scratch.bytes == std::vector<simnet::Byte>{simnet::Byte{7U}});
}

TEST_CASE("incremental AOI converges leave and re-entry", "[aoi][incremental][replication]")
{
    auto source_snapshot = make_snapshot(
        1U,
        {
            {.id = 1U,
             .classification = simnet::EntityClassification{2U},
             .position = {},
             .heading = {.z = 1.0F}},
            {.id = 2U,
             .classification = simnet::EntityClassification{1U},
             .position = {0.0F, 0.0F, 2.0F},
             .heading = {.z = 1.0F}},
        }
    );
    auto const source = simnet::InterestSource{
        .position = {},
        .forward = {.z = 1.0F},
        .source_entity_id = 1U,
    };
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Incremental;
    pipeline.incremental.max_entities_per_update = 1U;
    pipeline.area_of_interest = {
        .mode = simnet::AreaOfInterestMode::Radius,
        .radius = 5.0F,
    };
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto candidates = std::vector<std::uint32_t>{0U, 1U};
    auto first = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source_snapshot,
            .interest_source = &source,
            .candidate_indices = candidates,
        }
    );
    REQUIRE(first.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    auto replica = first.resulting_snapshot;

    source_snapshot.tick = 2U;
    source_snapshot.positions[1] = {0.0F, 0.0F, 8.0F};
    candidates = {0U};
    auto left = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source_snapshot,
            .replica_snapshot = &replica,
            .replica_sequence = first.update.sequence,
            .interest_source = &source,
            .candidate_indices = candidates,
        }
    );
    CHECK(left.report.delete_count == 1U);
    CHECK(left.resulting_snapshot.ids == std::vector<simnet::EntityNetId>{1U});
    replica = left.resulting_snapshot;

    source_snapshot.tick = 3U;
    source_snapshot.positions[1] = {0.0F, 0.0F, 2.0F};
    candidates = {0U, 1U};
    auto first_reentry = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source_snapshot,
            .replica_snapshot = &replica,
            .replica_sequence = left.update.sequence,
            .interest_source = &source,
            .candidate_indices = candidates,
        }
    );
    replica = first_reentry.resulting_snapshot;
    source_snapshot.tick = 4U;
    auto second_reentry = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source_snapshot,
            .replica_snapshot = &replica,
            .replica_sequence = first_reentry.update.sequence,
            .interest_source = &source,
            .candidate_indices = candidates,
        }
    );
    CHECK(second_reentry.resulting_snapshot.ids == std::vector<simnet::EntityNetId>{1U, 2U});
}

TEST_CASE("AOI candidates remain ordered through incremental wraparound", "[aoi][incremental]")
{
    auto snapshot = make_snapshot(
        1U,
        {
            {.id = 1U,
             .classification = simnet::EntityClassification{1U},
             .position = {-3.0F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 2U,
             .classification = simnet::EntityClassification{1U},
             .position = {-1.0F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 3U,
             .classification = simnet::EntityClassification{1U},
             .position = {1.0F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 4U,
             .classification = simnet::EntityClassification{1U},
             .position = {3.0F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
        }
    );
    auto const source = simnet::InterestSource{
        .position = {},
        .forward = {.z = 1.0F},
    };
    auto const candidates = query_candidates(snapshot, source.position, 5.0F);
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Incremental;
    pipeline.incremental.max_entities_per_update = 2U;
    pipeline.area_of_interest = {
        .mode = simnet::AreaOfInterestMode::Radius,
        .radius = 5.0F,
    };
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto first = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &snapshot,
            .interest_source = &source,
            .candidate_indices = candidates,
        }
    );
    REQUIRE(first.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    auto replica = first.resulting_snapshot;
    auto replica_sequence = first.update.sequence;

    auto encode_next = [&]() {
        ++snapshot.tick;
        auto encoded = simnet::encode_snapshot(
            pipeline,
            state,
            scratch,
            {
                .snapshot = &snapshot,
                .replica_snapshot = &replica,
                .replica_sequence = replica_sequence,
                .interest_source = &source,
                .candidate_indices = candidates,
            }
        );
        replica = encoded.resulting_snapshot;
        replica_sequence = encoded.update.sequence;
        return encoded;
    };

    CHECK(decoded_ids(pipeline, encode_next()) == std::vector<simnet::EntityNetId>{1U, 2U});
    CHECK(decoded_ids(pipeline, encode_next()) == std::vector<simnet::EntityNetId>{3U, 4U});
    CHECK(decoded_ids(pipeline, encode_next()) == std::vector<simnet::EntityNetId>{1U, 2U});
}

TEST_CASE("Delta AOI uses the selected acknowledged population", "[aoi][delta]")
{
    auto const baseline = make_snapshot(
        1U,
        {
            {.id = 1U,
             .classification = simnet::EntityClassification{2U},
             .position = {},
             .heading = {.z = 1.0F}},
            {.id = 2U,
             .classification = simnet::EntityClassification{1U},
             .position = {0.0F, 0.0F, 2.0F},
             .heading = {.z = 1.0F}},
        }
    );
    auto current = baseline;
    current.tick = 2U;
    current.positions[1] = {0.0F, 0.0F, 8.0F};
    auto const source = simnet::InterestSource{
        .position = {},
        .forward = {.z = 1.0F},
        .source_entity_id = 1U,
    };
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Delta;
    pipeline.area_of_interest = {
        .mode = simnet::AreaOfInterestMode::Radius,
        .radius = 5.0F,
    };
    auto state = simnet::ClientReplicationState{.next_sequence = 2U};
    auto scratch = simnet::PipelineScratch{};
    auto const candidates = std::array<std::uint32_t, 1>{0U};
    auto const encoded = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &current,
            .baseline_snapshot = &baseline,
            .baseline_sequence = 1U,
            .interest_source = &source,
            .candidate_indices = candidates,
        }
    );
    CHECK(encoded.report.snapshot_kind == simnet::SnapshotKind::Patch);
    CHECK(encoded.report.delete_count == 1U);
    CHECK(encoded.resulting_snapshot.ids == std::vector<simnet::EntityNetId>{1U});
}

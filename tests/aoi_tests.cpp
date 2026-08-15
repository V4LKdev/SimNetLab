#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <initializer_list>
#include <vector>

import simnet.core;
import simnet.pipeline;
import simnet.snapshot;

namespace
{
    [[nodiscard]] simnet::WorldSnapshot
    make_snapshot(simnet::Tick tick, std::initializer_list<simnet::EntityState> entities)
    {
        auto snapshot = simnet::WorldSnapshot{};
        snapshot.tick = tick;
        snapshot.reserve(entities.size());
        for (auto const& entity : entities)
        {
            snapshot.ids.push_back(entity.id);
            snapshot.classifications.push_back(entity.classification);
            snapshot.positions.push_back(entity.position);
            snapshot.headings.push_back(entity.heading);
            snapshot.hues.push_back(entity.hue);
        }
        return snapshot;
    }

    [[nodiscard]] std::vector<simnet::EntityNetId>
    decoded_ids(simnet::PipelineDefinition const& pipeline, simnet::EncodeOutput const& encoded)
    {
        auto state = simnet::ClientReplicationState{};
        auto const decoded =
            simnet::decode_update(pipeline, state, {.bytes = encoded.update.bytes});
        REQUIRE(decoded.report.valid);

        auto ids = std::vector<simnet::EntityNetId>{};
        ids.reserve(decoded.update.upserts.size());
        for (auto const& entity : decoded.update.upserts)
        {
            ids.push_back(entity.id);
        }
        return ids;
    }
}

TEST_CASE("radius AOI selects the inclusive local population", "[aoi][radius]")
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
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.area_of_interest = {
        .mode = simnet::AreaOfInterestMode::Radius,
        .radius = 4.0F,
    };

    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const candidates = std::array<std::uint32_t, 3>{1U, 2U, 3U};
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
    CHECK(encoded.report.area_of_interest.retained_count == 3U);
    CHECK(encoded.report.area_of_interest.culled_count == 1U);
}

TEST_CASE("FOV AOI selects the inclusive 3D cone", "[aoi][fov]")
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
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.area_of_interest = {
        .mode = simnet::AreaOfInterestMode::Fov,
        .radius = 10.0F,
        .fov_degrees = 120.0F,
    };

    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const candidates = std::array<std::uint32_t, 6>{1U, 2U, 3U, 4U, 5U, 6U};
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
}

TEST_CASE("disabled AOI preserves the complete population", "[aoi][none]")
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
    auto const encoded =
        simnet::encode_snapshot(simnet::PipelineDefinition{}, state, scratch, {.snapshot = &snapshot});

    CHECK(encoded.resulting_snapshot.ids == snapshot.ids);
    CHECK(encoded.report.area_of_interest.retained_count == snapshot.size());
}

TEST_CASE("incremental AOI converges after leave and re-entry", "[aoi][incremental]")
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
    auto reentry = simnet::encode_snapshot(
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
    replica = reentry.resulting_snapshot;

    source_snapshot.tick = 4U;
    auto converged = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source_snapshot,
            .replica_snapshot = &replica,
            .replica_sequence = reentry.update.sequence,
            .interest_source = &source,
            .candidate_indices = candidates,
        }
    );
    CHECK(converged.resulting_snapshot.ids == std::vector<simnet::EntityNetId>{1U, 2U});
}

TEST_CASE("Delta AOI removes entities outside the selected population", "[aoi][delta]")
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
    pipeline.techniques =
        simnet::PipelineTechniqueFlags::Delta | simnet::PipelineTechniqueFlags::DeltaFieldMask;
    pipeline.area_of_interest = {
        .mode = simnet::AreaOfInterestMode::Radius,
        .radius = 5.0F,
    };

    auto state =
        simnet::ClientReplicationState{.next_sequence = 2U, .level_of_detail_schedule = {}};
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
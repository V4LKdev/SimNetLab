#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <utility>
#include <vector>

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
}

TEST_CASE("distance LOD uses inclusive near and medium bands", "[lod][classification]")
{
    auto const source = make_snapshot(
        0U,
        {
            {.id = 1U,
             .classification = simnet::EntityClassification{1U},
             .position = {2.0F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 2U,
             .classification = simnet::EntityClassification{2U},
             .position = {4.0F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 3U,
             .classification = simnet::EntityClassification{3U},
             .position = {4.01F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 4U,
             .classification = simnet::EntityClassification{1U},
             .position = {8.0F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
            {.id = 5U,
             .classification = simnet::EntityClassification{2U},
             .position = {8.01F, 0.0F, 0.0F},
             .heading = {.z = 1.0F}},
        }
    );

    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const encoded = simnet::encode_snapshot(
        lod_pipeline(),
        state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );

    CHECK(encoded.report.level_of_detail.population.near == 2U);
    CHECK(encoded.report.level_of_detail.population.medium == 2U);
    CHECK(encoded.report.level_of_detail.population.far == 1U);
    CHECK(encoded.resulting_snapshot.classifications == source.classifications);
}

TEST_CASE("distance LOD emits partial Patches that reconstruct the represented state", "[lod][patch]")
{
    auto source = linear_snapshot(0U, 6U, 2.0F);
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto const pipeline = lod_pipeline();

    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};

    auto const full = simnet::encode_snapshot(
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

    auto const patch = simnet::encode_snapshot(
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
    CHECK(patch.report.upsert_count < source.size());

    auto const decoded = simnet::decode_update(
        pipeline,
        decode_state,
        {
            .bytes = patch.update.bytes,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(decoded.report.valid);

    auto reconstructed = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot(
            &full.resulting_snapshot,
            decoded.update,
            reconstructed
        )
            .valid
    );
    CHECK(same_snapshot(reconstructed, patch.resulting_snapshot));
}

TEST_CASE("Incremental LOD services persistent eligible work without starvation", "[lod][incremental]")
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

TEST_CASE("LOD recovery forces a lost deferred update to be represented again", "[lod][loss]")
{
    auto source = linear_snapshot(0U, 2U, 10.0F);
    auto const candidates = all_candidates(source);
    auto const interest = stationary_source();
    auto pipeline = lod_pipeline();
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;

    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};

    auto const full = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {.snapshot = &source, .interest_source = &interest, .candidate_indices = candidates}
    );

    auto const far_due = encode_state.level_of_detail_schedule[1].next_due_tick;
    source.tick = far_due;
    source.positions[1].y = 3.0F;

    auto const lost = simnet::encode_snapshot(
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
    REQUIRE(update_ids(scratch.logical_update) == std::vector<simnet::EntityNetId>{2U});

    auto const nominal_due_after_attempt = encode_state.level_of_detail_schedule[1].next_due_tick;
    auto const recovery_ids = std::vector<simnet::EntityNetId>{2U};
    ++source.tick;

    auto const repeated = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
            .recovery_upsert_ids = recovery_ids,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );

    CHECK(repeated.report.recovery_forced_addition_count == 1U);
    CHECK(
        repeated.report.level_of_detail.recovery_forced_count ==
        repeated.report.recovery_forced_addition_count
    );
    CHECK(encode_state.level_of_detail_schedule[1].next_due_tick == nominal_due_after_attempt);
    CHECK(update_ids(scratch.logical_update) == std::vector<simnet::EntityNetId>{2U});

    auto const decoded = simnet::decode_update(
        pipeline,
        decode_state,
        {
            .bytes = repeated.update.bytes,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(decoded.report.valid);

    auto reconstructed = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(
            &full.resulting_snapshot,
            decoded.update,
            reconstructed
        )
            .valid
    );
    CHECK(same_snapshot(reconstructed, repeated.resulting_snapshot));

    static_cast<void>(lost);
}

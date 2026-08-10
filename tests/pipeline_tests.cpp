#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

import simnet.core;
import simnet.pipeline;
import simnet.snapshot;

namespace
{
    constexpr simnet::EntityClassification known_classification{1U};
    constexpr simnet::EntityClassification unknown_classification{247U};
    constexpr auto encoded_update_header_bytes = std::size_t{45U};

    [[nodiscard]] simnet::WorldSnapshot
    make_snapshot(simnet::Tick tick, std::vector<simnet::EntityState> const& boids)
    {
        auto snapshot = simnet::WorldSnapshot{};
        snapshot.tick = tick;
        snapshot.reserve(boids.size());
        for (auto const& boid : boids)
        {
            snapshot.ids.push_back(boid.id);
            snapshot.classifications.push_back(boid.classification);
            snapshot.positions.push_back(boid.position);
            snapshot.headings.push_back(boid.heading);
            snapshot.hues.push_back(boid.hue);
        }
        return snapshot;
    }

    [[nodiscard]] simnet::WorldSnapshot make_linear_snapshot(simnet::Tick tick, std::uint32_t count)
    {
        auto boids = std::vector<simnet::EntityState>{};
        boids.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            boids.push_back({
                .id = static_cast<simnet::EntityNetId>(index + 1U),
                .classification = known_classification,
                .position = {static_cast<float>(index), 0.0F, 0.0F},
                .heading = {1.0F, 0.0F, 0.0F},
                .hue = static_cast<std::uint8_t>(index),
            });
        }
        return make_snapshot(tick, boids);
    }

    [[nodiscard]] bool same_binary32(float left, float right)
    {
        return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
    }

    [[nodiscard]] bool
    same_snapshot_bits(simnet::WorldSnapshot const& left, simnet::WorldSnapshot const& right)
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
            if (!same_binary32(left.positions[index].x, right.positions[index].x) ||
                !same_binary32(left.positions[index].y, right.positions[index].y) ||
                !same_binary32(left.positions[index].z, right.positions[index].z) ||
                !same_binary32(left.headings[index].x, right.headings[index].x) ||
                !same_binary32(left.headings[index].y, right.headings[index].y) ||
                !same_binary32(left.headings[index].z, right.headings[index].z))
            {
                return false;
            }
        }
        return true;
    }

    void write_u32(std::vector<simnet::Byte>& bytes, std::size_t offset, std::uint32_t value)
    {
        bytes[offset] = static_cast<simnet::Byte>((value >> 24U) & 0xFFU);
        bytes[offset + 1U] = static_cast<simnet::Byte>((value >> 16U) & 0xFFU);
        bytes[offset + 2U] = static_cast<simnet::Byte>((value >> 8U) & 0xFFU);
        bytes[offset + 3U] = static_cast<simnet::Byte>(value & 0xFFU);
    }
}

TEST_CASE("delta pipeline preserves baseline and patch semantics", "[pipeline][delta]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;

    auto const baseline = make_snapshot(
        0,
        {
            {.id = 1,
             .classification = known_classification,
             .position = {1.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 10},
            {.id = 2,
             .classification = known_classification,
             .position = {2.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 20},
            {.id = 3,
             .classification = known_classification,
             .position = {3.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 30},
        }
    );
    auto const current = make_snapshot(
        1,
        {
            {.id = 1,
             .classification = known_classification,
             .position = {1.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 10},
            {.id = 2,
             .classification = known_classification,
             .position = {20.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 20},
            {.id = 4,
             .classification = known_classification,
             .position = {4.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 40},
        }
    );

    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};

    auto const full =
        simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &baseline});
    REQUIRE(full.kind == simnet::EncodeResultKind::Update);
    REQUIRE(full.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    REQUIRE(full.report.baseline_sequence == 0);

    auto const full_decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = full.update.bytes});
    REQUIRE(full_decoded.report.valid);

    auto const delta = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        {
            .snapshot = &current,
            .baseline_snapshot = &baseline,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(delta.kind == simnet::EncodeResultKind::Update);
    REQUIRE(delta.report.baseline_sequence != 0U);
    REQUIRE(delta.report.snapshot_kind == simnet::SnapshotKind::Patch);
    REQUIRE(delta.report.baseline_sequence == full.update.sequence);
    REQUIRE(delta.report.upsert_count == 2);
    REQUIRE(delta.report.delete_count == 1);
    CHECK(delta.report.delta.candidate_count == 3U);
    CHECK(delta.report.delta.unchanged_count == 1U);
    CHECK(delta.report.delta.changed_existing_count == 1U);
    CHECK(delta.report.delta.spawned_count == 1U);
    CHECK(delta.report.delta.produced_upsert_count == delta.report.upsert_count);
    CHECK(delta.report.delta.whole_record_existing_upsert_count == 1U);
    CHECK(delta.report.delta.masked_existing_upsert_count == 0U);
    CHECK(delta.report.delta.complete_record_equivalent_bytes == 60U);
    CHECK(delta.report.delta.actual_upsert_representation_bytes == 60U);

    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = delta.update.bytes});
    REQUIRE(decoded.report.valid);
    REQUIRE(decoded.update.upserts.size() == 2);
    CHECK(decoded.update.upserts[0].id == 2);
    CHECK(decoded.update.upserts[1].id == 4);
    CHECK(decoded.update.deletes == std::vector<simnet::EntityNetId>{3});
}

TEST_CASE("raw Delta comparison uses exact binary32 values", "[pipeline][delta]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Delta;
    auto const source = make_snapshot(
        1U,
        {{.id = 1U,
          .classification = known_classification,
          .position = {0.0F, 1.0F, 2.0F},
          .heading = {1.0F, 0.0F, 0.0F},
          .hue = 4U}}
    );
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const full = simnet::encode_snapshot(pipeline, state, scratch, {.snapshot = &source});

    auto encode_against_full = [&](simnet::WorldSnapshot const& current)
    {
        return simnet::encode_snapshot(
            pipeline,
            state,
            scratch,
            {
                .snapshot = &current,
                .baseline_snapshot = &full.resulting_snapshot,
                .baseline_sequence = full.update.sequence,
            }
        );
    };

    auto equal = source;
    equal.tick = 2U;
    auto const equal_delta = encode_against_full(equal);
    CHECK(equal_delta.report.upsert_count == 0U);
    CHECK(equal_delta.report.delta.unchanged_count == 1U);

    auto changed = equal;
    changed.tick = 3U;
    changed.positions[0].x = 1.0F;
    auto const changed_delta = encode_against_full(changed);
    CHECK(changed_delta.report.upsert_count == 1U);
    CHECK(changed_delta.report.delta.changed_existing_count == 1U);

    auto signed_zero = equal;
    signed_zero.tick = 4U;
    signed_zero.positions[0].x = -0.0F;
    auto const signed_zero_delta = encode_against_full(signed_zero);
    REQUIRE(signed_zero_delta.report.upsert_count == 1U);
    CHECK(signed_zero_delta.report.delta.changed_existing_count == 1U);
    auto decode_state = simnet::ClientReplicationState{};
    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = signed_zero_delta.update.bytes});
    REQUIRE(decoded.report.valid);
    REQUIRE(decoded.update.upserts.size() == 1U);
    CHECK(same_binary32(decoded.update.upserts[0].position.x, -0.0F));
}

TEST_CASE(
    "quantized Delta compares canonical position and heading values",
    "[pipeline][delta][quantized]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques =
        simnet::PipelineTechniqueFlags::Delta | simnet::PipelineTechniqueFlags::Quantization;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(10.0F);
    auto source = make_snapshot(
        1U,
        {{.id = 1U,
          .classification = known_classification,
          .position = {0.0F, 10.0F, -10.0F},
          .heading = {1.0F, 0.0F, 0.0F},
          .hue = 4U}}
    );
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const full = simnet::encode_snapshot(pipeline, state, scratch, {.snapshot = &source});

    auto encode_against_full = [&](simnet::WorldSnapshot const& current)
    {
        return simnet::encode_snapshot(
            pipeline,
            state,
            scratch,
            {
                .snapshot = &current,
                .baseline_snapshot = &full.resulting_snapshot,
                .baseline_sequence = full.update.sequence,
            }
        );
    };

    source.tick = 2U;
    source.positions[0].x = 0.0001F;
    auto const sub_quantum = encode_against_full(source);
    CHECK(sub_quantum.report.upsert_count == 0U);
    CHECK(sub_quantum.report.delta.unchanged_count == 1U);

    source.tick = 3U;
    source.positions[0].x = 0.001F;
    auto const adjacent_bin = encode_against_full(source);
    CHECK(adjacent_bin.report.upsert_count == 1U);

    source.tick = 4U;
    source.positions[0] = {20.0F, 10.0F, -10.0F};
    auto clamped_baseline_source = source;
    clamped_baseline_source.tick = 5U;
    auto clamped_state = simnet::ClientReplicationState{};
    auto clamped_scratch = simnet::PipelineScratch{};
    auto const clamped_full = simnet::encode_snapshot(
        pipeline,
        clamped_state,
        clamped_scratch,
        {.snapshot = &clamped_baseline_source}
    );
    source.tick = 6U;
    source.positions[0].x = 30.0F;
    auto const same_clamped_boundary = simnet::encode_snapshot(
        pipeline,
        clamped_state,
        clamped_scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &clamped_full.resulting_snapshot,
            .baseline_sequence = clamped_full.update.sequence,
        }
    );
    CHECK(same_clamped_boundary.report.upsert_count == 0U);

    source.tick = 7U;
    source.positions[0].x = -20.0F;
    auto const opposite_clamped_boundary = simnet::encode_snapshot(
        pipeline,
        clamped_state,
        clamped_scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &clamped_full.resulting_snapshot,
            .baseline_sequence = clamped_full.update.sequence,
        }
    );
    CHECK(opposite_clamped_boundary.report.upsert_count == 1U);

    auto heading_source = make_snapshot(
        8U,
        {{.id = 1U, .classification = known_classification, .heading = {1.0F, 0.0F, 0.0F}}}
    );
    auto heading_state = simnet::ClientReplicationState{};
    auto heading_scratch = simnet::PipelineScratch{};
    auto const heading_full = simnet::encode_snapshot(
        pipeline,
        heading_state,
        heading_scratch,
        {.snapshot = &heading_source}
    );
    heading_source.tick = 9U;
    heading_source.headings[0] = simnet::normalize_or({1.0F, 0.000001F, 0.0F}, {.x = 1.0F});
    auto const same_heading = simnet::encode_snapshot(
        pipeline,
        heading_state,
        heading_scratch,
        {
            .snapshot = &heading_source,
            .baseline_snapshot = &heading_full.resulting_snapshot,
            .baseline_sequence = heading_full.update.sequence,
        }
    );
    CHECK(same_heading.report.upsert_count == 0U);
}

TEST_CASE(
    "octahedral and bit-packed Delta compare their canonical headings",
    "[pipeline][delta][quantized][bitpacked]"
)
{
    auto verify = [](bool bitpacked, simnet::Vec3f baseline_heading, simnet::Vec3f current_heading)
    {
        auto pipeline = simnet::PipelineDefinition{};
        pipeline.techniques = simnet::PipelineTechniqueFlags::Delta |
                              simnet::PipelineTechniqueFlags::Quantization |
                              simnet::PipelineTechniqueFlags::OctHeading;
        if (bitpacked)
        {
            pipeline.techniques |= simnet::PipelineTechniqueFlags::BitPacking;
        }
        auto source = make_snapshot(
            1U,
            {{.id = 1U, .classification = known_classification, .heading = baseline_heading}}
        );
        auto state = simnet::ClientReplicationState{};
        auto scratch = simnet::PipelineScratch{};
        auto const full = simnet::encode_snapshot(pipeline, state, scratch, {.snapshot = &source});
        source.tick = 2U;
        source.headings[0] = current_heading;
        auto const delta = simnet::encode_snapshot(
            pipeline,
            state,
            scratch,
            {
                .snapshot = &source,
                .baseline_snapshot = &full.resulting_snapshot,
                .baseline_sequence = full.update.sequence,
            }
        );
        CHECK(delta.report.delta.candidate_count == 1U);
        CHECK(delta.report.delta.unchanged_count == 1U);
        CHECK(delta.report.upsert_count == 0U);
    };

    auto const axis_perturbation = simnet::normalize_or({1.0F, 0.000001F, 0.0F}, {.x = 1.0F});
    verify(false, {1.0F, 0.0F, 0.0F}, axis_perturbation);
    verify(true, {1.0F, 0.0F, 0.0F}, axis_perturbation);

    auto const fold = simnet::normalize_or({1.0F, 0.0F, -1.0F}, {.x = 1.0F});
    auto const fold_perturbation = simnet::normalize_or({1.0F, 0.000001F, -1.0F}, {.x = 1.0F});
    verify(false, fold, fold_perturbation);

    auto const boundary_perturbation = simnet::normalize_or({1.0F, 0.0F, -0.000001F}, {.x = 1.0F});
    verify(false, {1.0F, 0.0F, 0.0F}, boundary_perturbation);
}

TEST_CASE(
    "Delta accounting distinguishes complete changed and spawned records",
    "[pipeline][delta][classification]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Delta;
    auto const baseline_source = make_linear_snapshot(1U, 3U);
    auto current = baseline_source;
    current.tick = 2U;
    current.classifications[1] = unknown_classification;
    current.hues[2] = 99U;
    current.ids.push_back(4U);
    current.classifications.push_back(known_classification);
    current.positions.push_back({4.0F, 0.0F, 0.0F});
    current.headings.push_back({1.0F, 0.0F, 0.0F});
    current.hues.push_back(4U);

    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const full =
        simnet::encode_snapshot(pipeline, state, scratch, {.snapshot = &baseline_source});
    auto const delta = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &current,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );

    CHECK(delta.report.delta.candidate_count == 4U);
    CHECK(delta.report.delta.unchanged_count == 1U);
    CHECK(delta.report.delta.changed_existing_count == 2U);
    CHECK(delta.report.delta.spawned_count == 1U);
    CHECK(delta.report.delta.produced_upsert_count == 3U);
    CHECK(delta.report.upsert_count == 3U);
    auto decode_state = simnet::ClientReplicationState{};
    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = delta.update.bytes});
    REQUIRE(decoded.report.valid);
    REQUIRE(decoded.update.upserts.size() == 3U);
    CHECK(decoded.update.upserts[0].id == 2U);
    CHECK(decoded.update.upserts[0].classification == unknown_classification);
    CHECK(decoded.update.upserts[0].position.x == baseline_source.positions[1].x);
    CHECK(decoded.update.upserts[1].id == 3U);
    CHECK(decoded.update.upserts[1].hue == 99U);
    CHECK(decoded.update.upserts[2].id == 4U);
}

TEST_CASE("deltas reconstruct from their exact retained baseline", "[pipeline][delta][replication]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;

    auto const baseline = make_snapshot(
        10,
        {
            {.id = 1,
             .classification = known_classification,
             .position = {1.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 10},
            {.id = 2,
             .classification = known_classification,
             .position = {2.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 20},
        }
    );
    auto const tick_11 = make_snapshot(
        11,
        {
            {.id = 1,
             .classification = unknown_classification,
             .position = {11.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 10},
            {.id = 2,
             .classification = known_classification,
             .position = {2.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 20},
        }
    );
    auto tick_12 = baseline;
    tick_12.tick = 12;

    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};

    auto const full =
        simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &baseline});
    REQUIRE(full.report.snapshot_kind == simnet::SnapshotKind::FullReplace);

    auto const first_delta = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        {
            .snapshot = &tick_11,
            .baseline_snapshot = &baseline,
            .baseline_sequence = full.update.sequence,
        }
    );
    auto const second_delta = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        {
            .snapshot = &tick_12,
            .baseline_snapshot = &baseline,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(first_delta.report.baseline_sequence == full.update.sequence);
    REQUIRE(second_delta.report.baseline_sequence == full.update.sequence);

    auto const decoded_full =
        simnet::decode_update(pipeline, decode_state, {.bytes = full.update.bytes});
    REQUIRE(decoded_full.report.valid);
    auto retained_baseline = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(
            nullptr,
            decoded_full.update,
            retained_baseline
        )
            .valid
    );

    auto const decoded_first =
        simnet::decode_update(pipeline, decode_state, {.bytes = first_delta.update.bytes});
    REQUIRE(decoded_first.report.valid);
    auto reconstructed_first = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(
            &retained_baseline,
            decoded_first.update,
            reconstructed_first
        )
            .valid
    );
    CHECK(reconstructed_first.positions[0].x == 11.0F);
    CHECK(reconstructed_first.classifications[0] == unknown_classification);

    auto const decoded_second =
        simnet::decode_update(pipeline, decode_state, {.bytes = second_delta.update.bytes});
    REQUIRE(decoded_second.report.valid);
    REQUIRE(decoded_second.report.baseline_sequence == full.update.sequence);
    auto reconstructed_second = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(
            &retained_baseline,
            decoded_second.update,
            reconstructed_second
        )
            .valid
    );
    CHECK(reconstructed_second.positions[0].x == 1.0F);
    CHECK(reconstructed_second.ids == retained_baseline.ids);
}

TEST_CASE(
    "pipeline validation permits composition and keeps representation prerequisites",
    "[pipeline]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Incremental;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Quantization;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::OctHeading;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::BitPacking;

    REQUIRE_NOTHROW(simnet::validate_pipeline_definition(pipeline));

    pipeline.techniques = simnet::PipelineTechniqueFlags::OctHeading;
    REQUIRE_THROWS(simnet::validate_pipeline_definition(pipeline));

    pipeline.techniques =
        simnet::PipelineTechniqueFlags::Quantization | simnet::PipelineTechniqueFlags::BitPacking;
    REQUIRE_THROWS(simnet::validate_pipeline_definition(pipeline));

    pipeline.techniques = simnet::PipelineTechniqueFlags::SendInterval;
    pipeline.send_interval.interval_ticks = 0U;
    REQUIRE_THROWS(simnet::validate_pipeline_definition(pipeline));

    pipeline.techniques = static_cast<simnet::PipelineTechniqueFlags>(1U << 31U);
    REQUIRE_THROWS(simnet::validate_pipeline_definition(pipeline));
}

TEST_CASE(
    "pipeline decoding delegates zero upsert ids to snapshot validation",
    "[pipeline][snapshot][validation]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    auto const snapshot = make_linear_snapshot(1U, 1U);
    auto encode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto encoded =
        simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &snapshot});
    REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
    REQUIRE(
        encoded.update.bytes.size() >= encoded_update_header_bytes + sizeof(simnet::EntityNetId)
    );

    auto constexpr payload_offset = encoded_update_header_bytes;
    for (std::size_t offset = 0; offset < sizeof(simnet::EntityNetId); ++offset)
    {
        encoded.update.bytes[payload_offset + offset] = simnet::Byte{};
    }

    auto decode_state = simnet::ClientReplicationState{};
    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});

    CHECK_FALSE(decoded.report.valid);
    CHECK(decoded.report.error.find("entity id zero is reserved") != std::string::npos);
    CHECK(decode_state.latest_remote_sequence == 0U);
}

TEST_CASE(
    "pipeline schema 5 preserves classifications in every record layout",
    "[pipeline][snapshot][classification]"
)
{
    auto const snapshot = make_snapshot(
        7U,
        {
            {.id = 1U,
             .classification = known_classification,
             .position = {-10.0F, 0.0F, 10.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 10U},
            {.id = 2U,
             .classification = unknown_classification,
             .position = {0.0F, 5.0F, -5.0F},
             .heading = {0.0F, 1.0F, 0.0F},
             .hue = 20U},
        }
    );

    auto round_trip = [&snapshot](simnet::PipelineDefinition pipeline, std::uint32_t record_bytes)
    {
        auto encode_state = simnet::ClientReplicationState{};
        auto decode_state = simnet::ClientReplicationState{};
        auto encode_scratch = simnet::PipelineScratch{};
        auto const encoded = simnet::encode_snapshot(
            pipeline,
            encode_state,
            encode_scratch,
            {.snapshot = &snapshot}
        );
        REQUIRE(encoded.update.bytes.size() >= 8U);
        CHECK(std::to_integer<std::uint8_t>(encoded.update.bytes[6U]) == 0U);
        CHECK(std::to_integer<std::uint8_t>(encoded.update.bytes[7U]) == 5U);
        CHECK(encoded.update.bytes.size() == encoded_update_header_bytes + record_bytes * 2U);

        auto const decoded =
            simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});
        REQUIRE(decoded.report.valid);
        REQUIRE(decoded.update.upserts.size() == 2U);
        CHECK(decoded.update.upserts[0].classification == known_classification);
        CHECK(decoded.update.upserts[1].classification == unknown_classification);
    };

    round_trip({}, 30U);

    auto quantized = simnet::PipelineDefinition{};
    quantized.techniques = simnet::PipelineTechniqueFlags::Quantization;
    round_trip(quantized, 18U);

    auto quantized_oct = quantized;
    quantized_oct.techniques |= simnet::PipelineTechniqueFlags::OctHeading;
    round_trip(quantized_oct, 16U);

    auto bitpacked = quantized_oct;
    bitpacked.techniques |= simnet::PipelineTechniqueFlags::BitPacking;
    round_trip(bitpacked, 16U);
}

TEST_CASE(
    "representation quality is opt-in and leaves encoded state unchanged",
    "[pipeline][representation][delta][incremental][recovery]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques =
        simnet::PipelineTechniqueFlags::Incremental | simnet::PipelineTechniqueFlags::Quantization |
        simnet::PipelineTechniqueFlags::OctHeading | simnet::PipelineTechniqueFlags::Delta |
        simnet::PipelineTechniqueFlags::BitPacking;
    pipeline.incremental.max_entities_per_update = 2U;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(10.0F);

    auto baseline = make_snapshot(
        1U,
        {
            {.id = 1U,
             .classification = known_classification,
             .position = {1.2345F, -2.3456F, 3.4567F},
             .heading = simnet::normalize_or({1.0F, 2.0F, 3.0F}, {.x = 1.0F}),
             .hue = 10U},
            {.id = 2U,
             .classification = known_classification,
             .position = {-2.3456F, 3.4567F, -4.5678F},
             .heading = simnet::normalize_or({-2.0F, 1.0F, 0.5F}, {.x = 1.0F}),
             .hue = 20U},
            {.id = 3U,
             .classification = known_classification,
             .position = {3.4567F, 4.5678F, -5.6789F},
             .heading = simnet::normalize_or({0.5F, -1.0F, 2.0F}, {.x = 1.0F}),
             .hue = 30U},
        }
    );
    auto state_without = simnet::ClientReplicationState{};
    auto state_with = simnet::ClientReplicationState{};
    auto scratch_without = simnet::PipelineScratch{};
    auto scratch_with = simnet::PipelineScratch{};
    auto const full_without =
        simnet::encode_snapshot(pipeline, state_without, scratch_without, {.snapshot = &baseline});
    auto const full_with = simnet::encode_snapshot(
        pipeline,
        state_with,
        scratch_with,
        {.snapshot = &baseline, .collect_representation_quality = true}
    );
    REQUIRE(full_without.update.bytes == full_with.update.bytes);
    REQUIRE(same_snapshot_bits(full_without.resulting_snapshot, full_with.resulting_snapshot));
    CHECK(full_without.report.representation.quality_sample_count == 0U);
    CHECK(full_with.report.representation.quality_sample_count == baseline.size());

    auto current = baseline;
    current.tick = 2U;
    current.positions[1].x += 0.75F;
    current.headings[2] = simnet::normalize_or({-1.0F, 3.0F, 0.25F}, {.x = 1.0F});
    auto const recovery_ids = std::array<simnet::EntityNetId, 1U>{3U};
    auto const patch_without = simnet::encode_snapshot(
        pipeline,
        state_without,
        scratch_without,
        {
            .snapshot = &current,
            .baseline_snapshot = &full_without.resulting_snapshot,
            .baseline_sequence = full_without.update.sequence,
            .recovery_upsert_ids = recovery_ids,
        }
    );
    auto const patch_with = simnet::encode_snapshot(
        pipeline,
        state_with,
        scratch_with,
        {
            .snapshot = &current,
            .baseline_snapshot = &full_with.resulting_snapshot,
            .baseline_sequence = full_with.update.sequence,
            .recovery_upsert_ids = recovery_ids,
            .collect_representation_quality = true,
        }
    );

    CHECK(patch_without.kind == patch_with.kind);
    CHECK(patch_without.update.sequence == patch_with.update.sequence);
    CHECK(patch_without.update.bytes == patch_with.update.bytes);
    CHECK(same_snapshot_bits(patch_without.resulting_snapshot, patch_with.resulting_snapshot));
    CHECK(state_without.next_sequence == state_with.next_sequence);
    CHECK(state_without.incremental_cursor == state_with.incremental_cursor);
    CHECK(state_without.incremental_seeded == state_with.incremental_seeded);
    CHECK(state_without.level_of_detail_seeded == state_with.level_of_detail_seeded);
    CHECK(scratch_without.selected_indices == scratch_with.selected_indices);
    CHECK(scratch_without.selected_delete_ids == scratch_with.selected_delete_ids);
    CHECK(scratch_without.prepared_record_bytes == scratch_with.prepared_record_bytes);
    CHECK(scratch_without.bytes == scratch_with.bytes);
    CHECK(
        scratch_without.logical_update.upserts.size() == scratch_with.logical_update.upserts.size()
    );
    CHECK(patch_without.report.representation.quality_sample_count == 0U);
    CHECK(patch_with.report.representation.quality_sample_count == patch_with.report.upsert_count);
}

TEST_CASE(
    "representation reports exact widths and truthful canonical quality",
    "[pipeline][representation][quality]"
)
{
    auto const snapshot = make_snapshot(
        1U,
        {
            {.id = 1U,
             .classification = known_classification,
             .position = {1.2345F, -2.3456F, 3.4567F},
             .heading = simnet::normalize_or({1.0F, 2.0F, 3.0F}, {.x = 1.0F}),
             .hue = 42U},
            {.id = 2U,
             .classification = unknown_classification,
             .position = {-4.5678F, 5.6789F, -6.7891F},
             .heading = simnet::normalize_or({-3.0F, 0.5F, 1.0F}, {.x = 1.0F}),
             .hue = 84U},
        }
    );

    auto pipelines = std::array<simnet::PipelineDefinition, 4U>{};
    pipelines[1].techniques = simnet::PipelineTechniqueFlags::Quantization;
    pipelines[2] = pipelines[1];
    pipelines[2].techniques |= simnet::PipelineTechniqueFlags::OctHeading;
    pipelines[3] = pipelines[2];
    pipelines[3].techniques |= simnet::PipelineTechniqueFlags::BitPacking;
    for (auto& pipeline : pipelines)
    {
        pipeline.quantization.position_bounds = simnet::make_centered_bounds(10.0F);
    }
    auto constexpr expected_widths = std::array<std::uint32_t, 4U>{30U, 18U, 16U, 16U};
    auto outputs = std::array<simnet::EncodeOutput, 4U>{};
    for (auto index = std::size_t{}; index < pipelines.size(); ++index)
    {
        auto state = simnet::ClientReplicationState{};
        auto scratch = simnet::PipelineScratch{};
        outputs[index] = simnet::encode_snapshot(
            pipelines[index],
            state,
            scratch,
            {.snapshot = &snapshot, .collect_representation_quality = true}
        );
        auto const& report = outputs[index].report.representation;
        CHECK(report.record_bytes == expected_widths[index]);
        CHECK(report.quality_sample_count == snapshot.size());
        CHECK(std::isfinite(report.position_error_sum));
        CHECK(std::isfinite(report.position_error_maximum));
        CHECK(std::isfinite(report.heading_angular_error_degrees_sum));
        CHECK(std::isfinite(report.heading_angular_error_degrees_maximum));
    }

    auto const& raw = outputs[0].report.representation;
    CHECK(raw.layout == simnet::EntityRecordLayout::Raw);
    CHECK(raw.position_error_sum == 0.0);
    CHECK(raw.position_error_maximum == 0.0);
    CHECK(raw.heading_angular_error_degrees_sum == 0.0);
    CHECK(raw.heading_angular_error_degrees_maximum == 0.0);

    auto const& quantized = outputs[1].report.representation;
    CHECK(quantized.layout == simnet::EntityRecordLayout::Quantized);
    CHECK(quantized.position_error_sum > 0.0);
    CHECK(quantized.position_error_maximum < 0.001);

    auto const& oct = outputs[2].report.representation;
    auto const& bit_packed = outputs[3].report.representation;
    CHECK(oct.layout == simnet::EntityRecordLayout::QuantizedOctHeading);
    CHECK(bit_packed.layout == simnet::EntityRecordLayout::BitPackedQuantizedOctHeading);
    CHECK(oct.record_bytes == 16U);
    CHECK(bit_packed.record_bytes == 16U);
    CHECK(oct.position_error_sum == bit_packed.position_error_sum);
    CHECK(oct.position_error_maximum == bit_packed.position_error_maximum);
    CHECK(oct.heading_angular_error_degrees_sum == bit_packed.heading_angular_error_degrees_sum);
    CHECK(
        oct.heading_angular_error_degrees_maximum ==
        bit_packed.heading_angular_error_degrees_maximum
    );
    REQUIRE(outputs[2].update.bytes.size() == outputs[3].update.bytes.size());
    CHECK(
        std::equal(
            outputs[2].update.bytes.begin() +
                static_cast<std::ptrdiff_t>(encoded_update_header_bytes),
            outputs[2].update.bytes.end(),
            outputs[3].update.bytes.begin() +
                static_cast<std::ptrdiff_t>(encoded_update_header_bytes)
        )
    );
    CHECK(same_snapshot_bits(outputs[2].resulting_snapshot, outputs[3].resulting_snapshot));
}

TEST_CASE(
    "reference FullReplace codec has a deterministic fixed-width wire layout",
    "[pipeline][reference][fullreplace]"
)
{
    auto const snapshot = make_snapshot(
        0x0102030405060708ULL,
        {
            {.id = 0x01020304U,
             .classification = simnet::EntityClassification{0xA5U},
             .position = {1.0F, -2.5F, 0.0F},
             .heading = {0.0F, 0.0F, 1.0F},
             .hue = 0x7FU},
        }
    );
    auto const pipeline = simnet::PipelineDefinition{};
    auto encode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};

    auto const encoded =
        simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &snapshot});

    auto constexpr expected = std::array<simnet::Byte, 75U>{
        simnet::Byte{0x53U}, simnet::Byte{0x4EU}, simnet::Byte{0x50U}, simnet::Byte{0x4CU},
        simnet::Byte{0x00U}, simnet::Byte{0x01U}, simnet::Byte{0x00U}, simnet::Byte{0x05U},
        simnet::Byte{0xBDU}, simnet::Byte{0x68U}, simnet::Byte{0xD3U}, simnet::Byte{0x28U},
        simnet::Byte{0x5EU}, simnet::Byte{0x58U}, simnet::Byte{0x27U}, simnet::Byte{0xC1U},
        simnet::Byte{0x00U}, simnet::Byte{0x01U}, simnet::Byte{0x02U}, simnet::Byte{0x03U},
        simnet::Byte{0x04U}, simnet::Byte{0x05U}, simnet::Byte{0x06U}, simnet::Byte{0x07U},
        simnet::Byte{0x08U}, simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U},
        simnet::Byte{0x01U}, simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U},
        simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U},
        simnet::Byte{0x01U}, simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U},
        simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U},
        simnet::Byte{0x1EU}, simnet::Byte{0x01U}, simnet::Byte{0x02U}, simnet::Byte{0x03U},
        simnet::Byte{0x04U}, simnet::Byte{0xA5U}, simnet::Byte{0x3FU}, simnet::Byte{0x80U},
        simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0xC0U}, simnet::Byte{0x20U},
        simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U},
        simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U},
        simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U},
        simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x3FU}, simnet::Byte{0x80U},
        simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x7FU},
    };

    REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
    CHECK(encoded.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    CHECK(encoded.report.baseline_sequence == 0U);
    CHECK(encoded.update.bytes == std::vector<simnet::Byte>{expected.begin(), expected.end()});

    auto decode_state = simnet::ClientReplicationState{};
    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});
    REQUIRE(decoded.report.valid);
    REQUIRE(decoded.update.upserts.size() == 1U);
    CHECK(decoded.report.baseline_sequence == 0U);
    CHECK(decoded.update.tick == snapshot.tick);
    CHECK(decoded.update.upserts[0].id == snapshot.ids[0]);
    CHECK(decoded.update.upserts[0].classification == snapshot.classifications[0]);
    CHECK(decoded.update.upserts[0].position.x == snapshot.positions[0].x);
    CHECK(decoded.update.upserts[0].position.y == snapshot.positions[0].y);
    CHECK(decoded.update.upserts[0].position.z == snapshot.positions[0].z);
    CHECK(decoded.update.upserts[0].heading.x == snapshot.headings[0].x);
    CHECK(decoded.update.upserts[0].heading.y == snapshot.headings[0].y);
    CHECK(decoded.update.upserts[0].heading.z == snapshot.headings[0].z);
    CHECK(decoded.update.upserts[0].hue == snapshot.hues[0]);
}

TEST_CASE(
    "reference FullReplace codec round-trips complete and empty snapshots",
    "[pipeline][reference][fullreplace]"
)
{
    auto const pipeline = simnet::PipelineDefinition{};
    auto const snapshots = std::array{
        make_snapshot(
            19U,
            {
                {.id = 2U,
                 .classification = simnet::EntityClassification{3U},
                 .position = {-12.5F, 7.25F, 0.125F},
                 .heading = {0.0F, 1.0F, 0.0F},
                 .hue = 7U},
                {.id = 99U,
                 .classification = unknown_classification,
                 .position = {40.0F, -0.25F, 8.0F},
                 .heading = {1.0F, 0.0F, 0.0F},
                 .hue = 240U},
            }
        ),
        make_snapshot(20U, {}),
    };

    for (auto const& snapshot : snapshots)
    {
        auto encode_state = simnet::ClientReplicationState{};
        auto encode_scratch = simnet::PipelineScratch{};
        auto const encoded = simnet::encode_snapshot(
            pipeline,
            encode_state,
            encode_scratch,
            {.snapshot = &snapshot}
        );
        REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
        CHECK(encoded.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
        CHECK(encoded.report.baseline_sequence == 0U);
        CHECK(encoded.report.upsert_count == snapshot.size());

        auto decode_state = simnet::ClientReplicationState{};
        auto const decoded =
            simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});
        REQUIRE(decoded.report.valid);
        CHECK(decoded.update.kind == simnet::SnapshotKind::FullReplace);
        CHECK(decoded.report.baseline_sequence == 0U);
        CHECK(decoded.update.tick == snapshot.tick);
        REQUIRE(decoded.update.upserts.size() == snapshot.size());
        for (std::size_t index = 0; index < snapshot.size(); ++index)
        {
            CHECK(decoded.update.upserts[index].id == snapshot.ids[index]);
            CHECK(decoded.update.upserts[index].classification == snapshot.classifications[index]);
            CHECK(decoded.update.upserts[index].position.x == snapshot.positions[index].x);
            CHECK(decoded.update.upserts[index].position.y == snapshot.positions[index].y);
            CHECK(decoded.update.upserts[index].position.z == snapshot.positions[index].z);
            CHECK(decoded.update.upserts[index].heading.x == snapshot.headings[index].x);
            CHECK(decoded.update.upserts[index].heading.y == snapshot.headings[index].y);
            CHECK(decoded.update.upserts[index].heading.z == snapshot.headings[index].z);
            CHECK(decoded.update.upserts[index].hue == snapshot.hues[index]);
        }
    }
}

TEST_CASE(
    "reference FullReplace decoding rejects malformed bytes transactionally",
    "[pipeline][reference][fullreplace][validation]"
)
{
    auto const pipeline = simnet::PipelineDefinition{};
    auto const snapshot = make_linear_snapshot(1U, 1U);
    auto encode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto const encoded =
        simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &snapshot});

    auto reject =
        [](simnet::PipelineDefinition const& decoder_pipeline, std::vector<simnet::Byte> bytes)
    {
        auto decode_state = simnet::ClientReplicationState{};
        auto const decoded =
            simnet::decode_update(decoder_pipeline, decode_state, {.bytes = bytes});
        CHECK_FALSE(decoded.report.valid);
        CHECK(decoded.update.empty());
        CHECK(decode_state.latest_remote_sequence == 0U);
    };

    auto truncated_header = std::vector<simnet::Byte>{
        encoded.update.bytes.begin(),
        encoded.update.bytes.begin() +
            static_cast<std::ptrdiff_t>(encoded_update_header_bytes - 1U),
    };
    reject(pipeline, std::move(truncated_header));

    auto truncated_record = encoded.update.bytes;
    truncated_record.pop_back();
    reject(pipeline, std::move(truncated_record));

    auto inconsistent_count = encoded.update.bytes;
    inconsistent_count[36U] = simnet::Byte{2U};
    reject(pipeline, std::move(inconsistent_count));

    auto impossible_count = encoded.update.bytes;
    for (std::size_t offset = 33U; offset < 37U; ++offset)
    {
        impossible_count[offset] = simnet::Byte{0xFFU};
    }
    reject(pipeline, std::move(impossible_count));

    auto trailing_bytes = encoded.update.bytes;
    trailing_bytes.push_back(simnet::Byte{0xFFU});
    reject(pipeline, std::move(trailing_bytes));

    auto unsupported_version = encoded.update.bytes;
    unsupported_version[7U] = simnet::Byte{3U};
    reject(pipeline, std::move(unsupported_version));

    auto unsupported_signature = encoded.update.bytes;
    unsupported_signature[8U] = simnet::Byte{};
    reject(pipeline, std::move(unsupported_signature));

    auto unsupported_kind = encoded.update.bytes;
    unsupported_kind[16U] = simnet::Byte{2U};
    reject(pipeline, std::move(unsupported_kind));

    auto quantized = simnet::PipelineDefinition{};
    quantized.techniques = simnet::PipelineTechniqueFlags::Quantization;
    reject(quantized, encoded.update.bytes);
}

TEST_CASE(
    "pipeline rejects zero wire classifications without sequence advancement",
    "[pipeline][snapshot][classification][validation]"
)
{
    auto const snapshot = make_linear_snapshot(1U, 1U);
    auto const pipeline = simnet::PipelineDefinition{};
    auto encode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto encoded =
        simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &snapshot});
    REQUIRE(encoded.update.bytes.size() == encoded_update_header_bytes + 30U);

    auto constexpr payload_offset = encoded_update_header_bytes;
    encoded.update.bytes[payload_offset + sizeof(simnet::EntityNetId)] = simnet::Byte{};

    auto decode_state = simnet::ClientReplicationState{};
    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});

    CHECK_FALSE(decoded.report.valid);
    CHECK(decoded.update.empty());
    CHECK(decoded.report.error.find("classification zero is reserved") != std::string::npos);
    CHECK(decode_state.latest_remote_sequence == 0U);

    encoded.update.bytes[payload_offset + sizeof(simnet::EntityNetId)] =
        static_cast<simnet::Byte>(known_classification.value());
    encoded.update.bytes[7U] = static_cast<simnet::Byte>(3U);
    auto const version_3 =
        simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});
    CHECK_FALSE(version_3.report.valid);
    CHECK(version_3.report.error == "unsupported encoded update version");
    CHECK(decode_state.latest_remote_sequence == 0U);
}

TEST_CASE(
    "pipeline decoding delegates zero delete ids to snapshot validation",
    "[pipeline][delta][snapshot][validation]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    auto const baseline = make_linear_snapshot(1U, 1U);
    auto current = simnet::WorldSnapshot{};
    current.tick = 2U;
    auto encode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto const full =
        simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &baseline});
    auto encoded = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        {
            .snapshot = &current,
            .baseline_snapshot = &baseline,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
    REQUIRE(encoded.report.delete_count == 1U);
    REQUIRE(
        encoded.update.bytes.size() >= encoded_update_header_bytes + sizeof(simnet::EntityNetId)
    );

    auto constexpr payload_offset = encoded_update_header_bytes;
    for (std::size_t offset = 0; offset < sizeof(simnet::EntityNetId); ++offset)
    {
        encoded.update.bytes[payload_offset + offset] = simnet::Byte{};
    }

    auto decode_state = simnet::ClientReplicationState{};
    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});

    CHECK_FALSE(decoded.report.valid);
    CHECK(decoded.report.error.find("entity id zero is reserved") != std::string::npos);
    CHECK(decode_state.latest_remote_sequence == 0U);
}

TEST_CASE(
    "pipeline decoding delegates FullReplace delete semantics to snapshot validation",
    "[pipeline][snapshot][validation]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    auto const baseline = make_linear_snapshot(1U, 1U);
    auto current = simnet::WorldSnapshot{};
    current.tick = 2U;
    auto encode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto const full =
        simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &baseline});
    auto encoded = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        {
            .snapshot = &current,
            .baseline_snapshot = &baseline,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(encoded.report.snapshot_kind == simnet::SnapshotKind::Patch);
    REQUIRE(encoded.report.delete_count == 1U);

    constexpr auto snapshot_kind_offset = std::size_t{16U};
    constexpr auto baseline_sequence_offset = std::size_t{29U};
    REQUIRE(encoded.update.bytes.size() > baseline_sequence_offset + 3U);
    encoded.update.bytes[snapshot_kind_offset] = simnet::Byte{};
    for (std::size_t offset = 0; offset < sizeof(simnet::SequenceId); ++offset)
    {
        encoded.update.bytes[baseline_sequence_offset + offset] = simnet::Byte{};
    }

    auto decode_state = simnet::ClientReplicationState{};
    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});

    CHECK_FALSE(decoded.report.valid);
    CHECK(
        decoded.report.error ==
        "decoded update is invalid: full replacement snapshot update deletes must be empty"
    );
    CHECK(decoded.update.empty());
    CHECK(decode_state.latest_remote_sequence == 0U);
}

TEST_CASE("checked pipeline encoding rejects invalid snapshots transactionally", "[pipeline]")
{
    auto snapshot = make_linear_snapshot(1U, 1U);
    snapshot.headings.front() = {};
    auto pipeline = simnet::PipelineDefinition{};
    auto encode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    encode_scratch.selected_indices.push_back(7U);
    encode_scratch.selected_delete_ids.push_back(9U);
    encode_scratch.bytes.push_back(simnet::Byte{42U});

    CHECK_THROWS(
        simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &snapshot})
    );
    CHECK(encode_state.next_sequence == 1U);
    CHECK(encode_state.incremental_cursor == 0U);
    CHECK(encode_scratch.selected_indices == std::vector<std::uint32_t>{7U});
    CHECK(encode_scratch.selected_delete_ids == std::vector<simnet::EntityNetId>{9U});
    CHECK(encode_scratch.bytes == std::vector<simnet::Byte>{simnet::Byte{42U}});
}

TEST_CASE("unchecked pipeline encoding rejects control misuse transactionally", "[pipeline]")
{
    auto const snapshot = make_linear_snapshot(1U, 1U);
    REQUIRE(simnet::validate_world_snapshot(snapshot).valid);
    auto pipeline = simnet::PipelineDefinition{};
    auto encode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    encode_scratch.selected_indices.push_back(7U);
    encode_scratch.selected_delete_ids.push_back(9U);
    encode_scratch.bytes.push_back(simnet::Byte{42U});

    SECTION("null current snapshot")
    {
        CHECK_THROWS(simnet::encode_snapshot_unchecked(pipeline, encode_state, encode_scratch, {}));
    }

    SECTION("baseline without delta technique")
    {
        CHECK_THROWS(
            simnet::encode_snapshot_unchecked(
                pipeline,
                encode_state,
                encode_scratch,
                {
                    .snapshot = &snapshot,
                    .baseline_snapshot = &snapshot,
                    .baseline_sequence = 1U,
                }
            )
        );
    }

    CHECK(encode_state.next_sequence == 1U);
    CHECK(encode_state.incremental_cursor == 0U);
    CHECK(encode_scratch.selected_indices == std::vector<std::uint32_t>{7U});
    CHECK(encode_scratch.selected_delete_ids == std::vector<simnet::EntityNetId>{9U});
    CHECK(encode_scratch.bytes == std::vector<simnet::Byte>{simnet::Byte{42U}});
}

TEST_CASE(
    "incremental pipeline seeds complete membership before scheduled emissions",
    "[pipeline][incremental]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Incremental;
    pipeline.send_interval.interval_ticks = 2;
    pipeline.incremental.max_entities_per_update = 4;

    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto emitted_index = std::size_t{};
    auto replica = simnet::WorldSnapshot{};
    auto replica_sequence = simnet::SequenceId{};
    auto const expected_ids = std::array{
        std::vector<simnet::EntityNetId>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
        std::vector<simnet::EntityNetId>{1, 2, 3, 4},
        std::vector<simnet::EntityNetId>{5, 6, 7, 8},
    };

    for (simnet::Tick tick = 0; tick < 5; ++tick)
    {
        auto const snapshot = make_linear_snapshot(tick, 10);
        auto const encoded = simnet::encode_snapshot(
            pipeline,
            encode_state,
            encode_scratch,
            {
                .snapshot = &snapshot,
                .replica_snapshot = encode_state.incremental_seeded ? &replica : nullptr,
                .replica_sequence = encode_state.incremental_seeded ? replica_sequence : 0U,
            }
        );

        if ((tick % 2U) != 0U)
        {
            CHECK(encoded.kind == simnet::EncodeResultKind::Skipped);
            continue;
        }

        REQUIRE(emitted_index < expected_ids.size());
        REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
        CHECK(encoded.update.sequence == emitted_index + 1U);
        CHECK(
            encoded.report.snapshot_kind ==
            (emitted_index == 0U ? simnet::SnapshotKind::FullReplace : simnet::SnapshotKind::Patch)
        );
        CHECK(encoded.report.upsert_count == expected_ids[emitted_index].size());
        CHECK(
            encoded.report.baseline_sequence ==
            (emitted_index == 0U ? 0U : static_cast<simnet::SequenceId>(emitted_index))
        );

        auto const decoded =
            simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});
        REQUIRE(decoded.report.valid);

        auto ids = std::vector<simnet::EntityNetId>{};
        ids.reserve(decoded.update.upserts.size());
        for (auto const& boid : decoded.update.upserts)
        {
            ids.push_back(boid.id);
        }
        CHECK(ids == expected_ids[emitted_index]);
        replica = encoded.resulting_snapshot;
        replica_sequence = encoded.update.sequence;
        ++emitted_index;
    }

    CHECK(emitted_index == expected_ids.size());
    CHECK(encode_state.incremental_cursor == 8);
    CHECK(encode_state.next_sequence == 4);
}

TEST_CASE("every Patch requires an explicit nonzero baseline", "[pipeline][incremental]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Incremental;
    pipeline.incremental.max_entities_per_update = 1U;
    auto encode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto source = make_linear_snapshot(1U, 2U);
    auto first = simnet::encode_snapshot(pipeline, encode_state, scratch, {.snapshot = &source});
    auto second = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {
            .snapshot = &source,
            .replica_snapshot = &first.resulting_snapshot,
            .replica_sequence = first.update.sequence,
        }
    );
    REQUIRE(second.report.snapshot_kind == simnet::SnapshotKind::Patch);
    REQUIRE(second.report.baseline_sequence == first.update.sequence);

    constexpr auto baseline_sequence_offset = std::size_t{29U};
    for (auto offset = std::size_t{}; offset < 4U; ++offset)
    {
        second.update.bytes[baseline_sequence_offset + offset] = simnet::Byte{};
    }
    auto decode_state = simnet::ClientReplicationState{};
    REQUIRE(
        simnet::decode_update(pipeline, decode_state, {.bytes = first.update.bytes}).report.valid
    );
    auto const rejected =
        simnet::decode_update(pipeline, decode_state, {.bytes = second.update.bytes});
    CHECK_FALSE(rejected.report.valid);
    CHECK(rejected.report.error.find("patch baseline sequence 0") != std::string::npos);
    CHECK(decode_state.latest_remote_sequence == first.update.sequence);
}

TEST_CASE(
    "incremental patches converge removals spawns and an empty population",
    "[pipeline][incremental][replication]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Incremental;
    pipeline.incremental.max_entities_per_update = 2U;

    SECTION("empty first population still seeds with FullReplace")
    {
        auto empty = simnet::WorldSnapshot{};
        empty.tick = 1U;
        auto state = simnet::ClientReplicationState{};
        auto scratch = simnet::PipelineScratch{};
        auto const encoded =
            simnet::encode_snapshot(pipeline, state, scratch, {.snapshot = &empty});
        CHECK(encoded.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
        CHECK(encoded.report.upsert_count == 0U);
        CHECK(encoded.report.delete_count == 0U);
        CHECK(encoded.resulting_snapshot.empty());
        CHECK(state.incremental_seeded);
        CHECK(state.incremental_cursor == 0U);
    }

    SECTION("later populations converge through sorted patches")
    {

        auto state = simnet::ClientReplicationState{};
        auto scratch = simnet::PipelineScratch{};
        auto source = make_linear_snapshot(1U, 3U);
        auto first = simnet::encode_snapshot(pipeline, state, scratch, {.snapshot = &source});
        REQUIRE(first.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
        CHECK(first.report.upsert_count == 3U);
        CHECK(first.report.delete_count == 0U);
        CHECK(state.incremental_cursor == 0U);
        auto replica = first.resulting_snapshot;

        source.tick = 2U;
        source.ids.erase(source.ids.begin() + 1);
        source.classifications.erase(source.classifications.begin() + 1);
        source.positions.erase(source.positions.begin() + 1);
        source.headings.erase(source.headings.begin() + 1);
        source.hues.erase(source.hues.begin() + 1);
        source.ids.push_back(4U);
        source.classifications.push_back(known_classification);
        source.positions.push_back({4.0F, 0.0F, 0.0F});
        source.headings.push_back({1.0F, 0.0F, 0.0F});
        source.hues.push_back(4U);
        auto second = simnet::encode_snapshot(
            pipeline,
            state,
            scratch,
            {.snapshot = &source, .replica_snapshot = &replica, .replica_sequence = 1U}
        );
        REQUIRE(second.report.snapshot_kind == simnet::SnapshotKind::Patch);
        CHECK(second.report.delete_count == 1U);
        CHECK(scratch.logical_update.deletes == std::vector<simnet::EntityNetId>{2U});
        CHECK(second.resulting_snapshot.ids == std::vector<simnet::EntityNetId>{1U, 3U});
        replica = second.resulting_snapshot;

        source.tick = 3U;
        auto third = simnet::encode_snapshot(
            pipeline,
            state,
            scratch,
            {.snapshot = &source, .replica_snapshot = &replica, .replica_sequence = 2U}
        );
        CHECK(third.resulting_snapshot.ids == std::vector<simnet::EntityNetId>{1U, 3U, 4U});
        replica = third.resulting_snapshot;

        auto empty = simnet::WorldSnapshot{};
        empty.tick = 4U;
        auto fourth = simnet::encode_snapshot(
            pipeline,
            state,
            scratch,
            {.snapshot = &empty, .replica_snapshot = &replica, .replica_sequence = 3U}
        );
        CHECK(fourth.report.upsert_count == 0U);
        CHECK(fourth.report.delete_count == 3U);
        CHECK(fourth.resulting_snapshot.empty());
    }
}

TEST_CASE("send interval emits the documented deterministic tick sequence", "[pipeline][cadence]")
{
    auto cadence_disabled = simnet::PipelineDefinition{};
    auto cadence_disabled_state = simnet::ClientReplicationState{};
    auto cadence_disabled_scratch = simnet::PipelineScratch{};
    for (simnet::Tick tick = 0; tick < 6U; ++tick)
    {
        auto const snapshot = make_linear_snapshot(tick, 1U);
        auto const encoded = simnet::encode_snapshot(
            cadence_disabled,
            cadence_disabled_state,
            cadence_disabled_scratch,
            {.snapshot = &snapshot}
        );
        CHECK(encoded.kind == simnet::EncodeResultKind::Update);
    }

    auto every_tick = simnet::PipelineDefinition{};
    every_tick.techniques = simnet::PipelineTechniqueFlags::SendInterval;
    auto every_tick_state = simnet::ClientReplicationState{};
    auto every_tick_scratch = simnet::PipelineScratch{};
    for (simnet::Tick tick = 0; tick < 6U; ++tick)
    {
        auto const snapshot = make_linear_snapshot(tick, 1U);
        auto const encoded = simnet::encode_snapshot(
            every_tick,
            every_tick_state,
            every_tick_scratch,
            {.snapshot = &snapshot}
        );
        CHECK(simnet::should_emit_snapshot(every_tick, tick));
        CHECK(encoded.kind == simnet::EncodeResultKind::Update);
    }

    auto every_third = simnet::PipelineDefinition{};
    every_third.techniques = simnet::PipelineTechniqueFlags::SendInterval;
    every_third.send_interval.interval_ticks = 3U;
    auto every_third_state = simnet::ClientReplicationState{};
    auto every_third_scratch = simnet::PipelineScratch{};
    auto const expected_kinds = std::array{
        simnet::EncodeResultKind::Update,
        simnet::EncodeResultKind::Skipped,
        simnet::EncodeResultKind::Skipped,
        simnet::EncodeResultKind::Update,
        simnet::EncodeResultKind::Skipped,
        simnet::EncodeResultKind::Skipped,
        simnet::EncodeResultKind::Update,
        simnet::EncodeResultKind::Skipped,
    };

    for (simnet::Tick tick = 0; tick < expected_kinds.size(); ++tick)
    {
        auto const snapshot = make_linear_snapshot(tick, 1U);
        auto const encoded = simnet::encode_snapshot(
            every_third,
            every_third_state,
            every_third_scratch,
            {.snapshot = &snapshot}
        );
        CHECK(
            simnet::should_emit_snapshot(every_third, tick) ==
            (expected_kinds[tick] == simnet::EncodeResultKind::Update)
        );
        CHECK(encoded.kind == expected_kinds[tick]);
        if (encoded.kind == simnet::EncodeResultKind::Skipped)
        {
            CHECK(encoded.update.bytes.empty());
        }
    }
    CHECK(every_third_state.next_sequence == 4U);

    every_third.send_interval.interval_ticks = 0U;
    REQUIRE_THROWS(simnet::should_emit_snapshot(every_third, 0U));
}

TEST_CASE(
    "send interval skips preserve delta scheduling, baseline, and scratch state",
    "[pipeline][cadence][incremental][delta]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::SendInterval |
                          simnet::PipelineTechniqueFlags::Incremental |
                          simnet::PipelineTechniqueFlags::Delta;
    pipeline.send_interval.interval_ticks = 3U;
    pipeline.incremental.max_entities_per_update = 2U;

    auto const baseline = make_linear_snapshot(0U, 4U);
    auto current = baseline;
    current.positions[1].x = 11.0F;
    current.positions[2].x = 12.0F;
    auto encode_state = simnet::ClientReplicationState{
        .next_sequence = 7U,
        .incremental_cursor = 1U,
        .incremental_seeded = true,
        .level_of_detail_schedule = {},
    };
    auto scratch = simnet::PipelineScratch{};
    scratch.selected_indices = {9U};
    scratch.selected_delete_ids = {10U};
    scratch.relevant_snapshot = baseline;
    scratch.logical_update.tick = 19U;
    scratch.bytes = {simnet::Byte{11U}};

    for (simnet::Tick tick : {1U, 2U})
    {
        current.tick = tick;
        auto const skipped = simnet::encode_snapshot(
            pipeline,
            encode_state,
            scratch,
            {
                .snapshot = &current,
                .baseline_snapshot = &baseline,
                .baseline_sequence = 5U,
            }
        );
        CHECK(skipped.kind == simnet::EncodeResultKind::Skipped);
        CHECK(skipped.update.bytes.empty());
        CHECK(encode_state.next_sequence == 7U);
        CHECK(encode_state.incremental_cursor == 1U);
        CHECK(encode_state.incremental_seeded);
        CHECK(scratch.selected_indices == std::vector<std::uint32_t>{9U});
        CHECK(scratch.selected_delete_ids == std::vector<simnet::EntityNetId>{10U});
        CHECK(scratch.relevant_snapshot.ids == baseline.ids);
        CHECK(scratch.logical_update.tick == 19U);
        CHECK(scratch.bytes == std::vector<simnet::Byte>{simnet::Byte{11U}});
    }

    current.tick = 3U;
    auto const emitted = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {
            .snapshot = &current,
            .baseline_snapshot = &baseline,
            .baseline_sequence = 5U,
        }
    );
    REQUIRE(emitted.kind == simnet::EncodeResultKind::Update);
    CHECK(emitted.update.sequence == 7U);
    CHECK(emitted.report.baseline_sequence == 5U);
    CHECK(emitted.report.snapshot_kind == simnet::SnapshotKind::Patch);
    CHECK(emitted.report.upsert_count == 2U);
    CHECK(encode_state.incremental_cursor == 3U);

    auto decode_state = simnet::ClientReplicationState{};
    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = emitted.update.bytes});
    REQUIRE(decoded.report.valid);
    CHECK(decoded.update.upserts[0].id == 2U);
    CHECK(decoded.update.upserts[1].id == 3U);

    auto baseline_less_state = simnet::ClientReplicationState{
        .incremental_cursor = 2U,
        .level_of_detail_schedule = {},
    };
    auto baseline_less_scratch = simnet::PipelineScratch{};
    auto const baseline_less = simnet::encode_snapshot(
        pipeline,
        baseline_less_state,
        baseline_less_scratch,
        {.snapshot = &current}
    );
    REQUIRE(baseline_less.kind == simnet::EncodeResultKind::Update);
    CHECK(baseline_less.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    CHECK(baseline_less.report.baseline_sequence == 0U);
    CHECK(baseline_less.report.upsert_count == current.size());
    CHECK(baseline_less_state.incremental_cursor == 2U);
}

TEST_CASE(
    "incremental delta composition filters scheduled upserts before representation encoding",
    "[pipeline][incremental][delta][quantized][bitpacked]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques =
        simnet::PipelineTechniqueFlags::Incremental | simnet::PipelineTechniqueFlags::Delta |
        simnet::PipelineTechniqueFlags::Quantization | simnet::PipelineTechniqueFlags::OctHeading |
        simnet::PipelineTechniqueFlags::BitPacking | simnet::PipelineTechniqueFlags::DeltaFieldMask;
    pipeline.incremental.max_entities_per_update = 3;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(100.0F);

    auto const baseline = make_linear_snapshot(10, 6);
    auto current = make_linear_snapshot(11, 5);
    current.positions[1].x = 20.0F;
    current.classifications[2] = unknown_classification;
    current.positions[3].x = 40.0F;
    current.ids.push_back(7);
    current.classifications.push_back(known_classification);
    current.positions.push_back({60.0F, 0.0F, 0.0F});
    current.headings.push_back({1.0F, 0.0F, 0.0F});
    current.hues.push_back(6);
    REQUIRE(simnet::validate_world_snapshot(current).valid);

    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};

    auto const baseline_less =
        simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &baseline});
    REQUIRE(baseline_less.kind == simnet::EncodeResultKind::Update);
    CHECK(baseline_less.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    CHECK(baseline_less.report.baseline_sequence == 0);
    CHECK(baseline_less.report.upsert_count == baseline.size());
    CHECK(encode_state.incremental_cursor == 0);

    auto const decoded_baseline_less =
        simnet::decode_update(pipeline, decode_state, {.bytes = baseline_less.update.bytes});
    REQUIRE(decoded_baseline_less.report.valid);
    CHECK(decoded_baseline_less.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    CHECK(decoded_baseline_less.update.upserts.size() == baseline.size());

    auto encode_and_decode = [&]
    {
        auto const encoded = simnet::encode_snapshot(
            pipeline,
            encode_state,
            encode_scratch,
            {
                .snapshot = &current,
                .baseline_snapshot = &baseline_less.resulting_snapshot,
                .baseline_sequence = baseline_less.update.sequence,
            }
        );
        REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
        REQUIRE(encoded.report.baseline_sequence != 0U);
        auto const decoded = simnet::decode_update(
            pipeline,
            decode_state,
            {
                .bytes = encoded.update.bytes,
                .baseline_snapshot = &baseline_less.resulting_snapshot,
                .baseline_sequence = baseline_less.update.sequence,
            }
        );
        REQUIRE(decoded.report.valid);
        return decoded.update;
    };

    auto const first = encode_and_decode();
    REQUIRE(first.upserts.size() == 2);
    CHECK(first.upserts[0].id == 2);
    CHECK(first.upserts[1].id == 3);
    CHECK(first.deletes == std::vector<simnet::EntityNetId>{6});
    CHECK(encode_state.incremental_cursor == 3);

    auto first_reconstructed = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(
            &baseline_less.resulting_snapshot,
            first,
            first_reconstructed
        )
            .valid
    );
    CHECK(first_reconstructed.ids == std::vector<simnet::EntityNetId>{1, 2, 3, 4, 5});
    CHECK(first_reconstructed.positions[3].x == baseline_less.resulting_snapshot.positions[3].x);

    auto const second = encode_and_decode();
    REQUIRE(second.upserts.size() == 2);
    CHECK(second.upserts[0].id == 4);
    CHECK(second.upserts[1].id == 7);
    CHECK(second.deletes == std::vector<simnet::EntityNetId>{6});
    CHECK(encode_state.incremental_cursor == 0);

    auto second_reconstructed = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(
            &baseline_less.resulting_snapshot,
            second,
            second_reconstructed
        )
            .valid
    );
    CHECK(second_reconstructed.ids == std::vector<simnet::EntityNetId>{1, 2, 3, 4, 5, 7});
}

TEST_CASE(
    "canonically unchanged incremental Delta candidates still advance the fair cursor",
    "[pipeline][incremental][delta][quantized]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Incremental |
                          simnet::PipelineTechniqueFlags::Delta |
                          simnet::PipelineTechniqueFlags::Quantization;
    pipeline.incremental.max_entities_per_update = 1U;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(10.0F);
    auto source = make_linear_snapshot(1U, 2U);
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const full = simnet::encode_snapshot(pipeline, state, scratch, {.snapshot = &source});

    source.tick = 2U;
    source.positions[0].x = 0.0001F;
    source.positions[1].x = 2.0F;
    auto const unchanged = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    CHECK(unchanged.report.delta.candidate_count == 1U);
    CHECK(unchanged.report.delta.unchanged_count == 1U);
    CHECK(unchanged.report.upsert_count == 0U);
    CHECK(state.incremental_cursor == 1U);

    source.tick = 3U;
    auto const changed = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    CHECK(changed.report.delta.candidate_count == 1U);
    CHECK(changed.report.delta.changed_existing_count == 1U);
    CHECK(changed.report.upsert_count == 1U);
    CHECK(scratch.logical_update.upserts[0].id == 2U);
    CHECK(state.incremental_cursor == 0U);
}

TEST_CASE(
    "quantized octahedral bit-packed snapshots round-trip",
    "[pipeline][quantized][bitpacked]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Quantization;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::OctHeading;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::BitPacking;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(100.0F);

    auto const snapshot = make_snapshot(
        7,
        {
            {.id = 1,
             .classification = known_classification,
             .position = {-100.0F, 0.0F, 100.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 10},
            {.id = 2,
             .classification = unknown_classification,
             .position = {0.0F, 25.0F, -50.0F},
             .heading = {0.0F, 1.0F, 0.0F},
             .hue = 20},
            {.id = 3,
             .classification = simnet::EntityClassification{2U},
             .position = {100.0F, -100.0F, 0.0F},
             .heading = {0.0F, 0.0F, 1.0F},
             .hue = 30},
        }
    );
    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};

    auto const encoded =
        simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &snapshot});
    REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
    CHECK(encoded.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    CHECK(encoded.report.upsert_count == 3);
    CHECK(encoded.update.bytes.size() == encoded_update_header_bytes + 48U);

    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});
    REQUIRE(decoded.report.valid);
    REQUIRE(decoded.update.upserts.size() == 3);
    CHECK(decoded.update.tick == 7);
    CHECK(decoded.update.upserts[0].id == 1);
    CHECK(decoded.update.upserts[1].id == 2);
    CHECK(decoded.update.upserts[2].id == 3);
    CHECK(decoded.update.upserts[0].classification == known_classification);
    CHECK(decoded.update.upserts[1].classification == unknown_classification);
    CHECK(decoded.update.upserts[2].classification == simnet::EntityClassification{2U});
    CHECK(simnet::validate_client_snapshot_patch(decoded.update).valid);
}

TEST_CASE(
    "encoded resulting snapshots equal exact quantized Client reconstruction",
    "[pipeline][quantized][replication]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Quantization |
                          simnet::PipelineTechniqueFlags::OctHeading |
                          simnet::PipelineTechniqueFlags::BitPacking;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(10.0F);
    auto const source = make_snapshot(
        1U,
        {{.id = 1U,
          .classification = known_classification,
          .position = {1.2345F, -2.3456F, 3.4567F},
          .heading = {0.70710677F, 0.70710677F, 0.0F},
          .hue = 42U}}
    );
    auto encode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const encoded =
        simnet::encode_snapshot(pipeline, encode_state, scratch, {.snapshot = &source});
    auto decode_state = simnet::ClientReplicationState{};
    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});
    REQUIRE(decoded.report.valid);
    auto reconstructed = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(nullptr, decoded.update, reconstructed).valid
    );

    REQUIRE(encoded.resulting_snapshot.size() == reconstructed.size());
    CHECK(encoded.resulting_snapshot.ids == reconstructed.ids);
    CHECK(encoded.resulting_snapshot.classifications == reconstructed.classifications);
    CHECK(encoded.resulting_snapshot.positions[0].x == reconstructed.positions[0].x);
    CHECK(encoded.resulting_snapshot.positions[0].y == reconstructed.positions[0].y);
    CHECK(encoded.resulting_snapshot.positions[0].z == reconstructed.positions[0].z);
    CHECK(encoded.resulting_snapshot.headings[0].x == reconstructed.headings[0].x);
    CHECK(encoded.resulting_snapshot.headings[0].y == reconstructed.headings[0].y);
    CHECK(encoded.resulting_snapshot.headings[0].z == reconstructed.headings[0].z);
    CHECK(encoded.resulting_snapshot.hues == reconstructed.hues);
    CHECK(encoded.resulting_snapshot.positions[0].x != source.positions[0].x);

    auto delta_pipeline = pipeline;
    delta_pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    auto current = source;
    current.tick = 2U;
    current.positions[0].x = 5.4321F;
    auto delta_state =
        simnet::ClientReplicationState{.next_sequence = 2U, .level_of_detail_schedule = {}};
    auto delta_scratch = simnet::PipelineScratch{};
    auto const delta = simnet::encode_snapshot(
        delta_pipeline,
        delta_state,
        delta_scratch,
        {
            .snapshot = &current,
            .baseline_snapshot = &encoded.resulting_snapshot,
            .baseline_sequence = encoded.update.sequence,
        }
    );
    auto delta_decode_state = simnet::ClientReplicationState{};
    auto const decoded_delta =
        simnet::decode_update(delta_pipeline, delta_decode_state, {.bytes = delta.update.bytes});
    REQUIRE(decoded_delta.report.valid);
    auto reconstructed_delta = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(
            &encoded.resulting_snapshot,
            decoded_delta.update,
            reconstructed_delta
        )
            .valid
    );
    CHECK(delta.resulting_snapshot.positions[0].x == reconstructed_delta.positions[0].x);
    CHECK(delta.resulting_snapshot.headings[0].x == reconstructed_delta.headings[0].x);
}

TEST_CASE(
    "every complete-record layout retains the exact decoded Delta result",
    "[pipeline][delta][replication]"
)
{
    auto pipelines = std::array<simnet::PipelineDefinition, 4>{};
    pipelines[0].techniques = simnet::PipelineTechniqueFlags::Delta;
    pipelines[1].techniques =
        simnet::PipelineTechniqueFlags::Delta | simnet::PipelineTechniqueFlags::Quantization;
    pipelines[2] = pipelines[1];
    pipelines[2].techniques |= simnet::PipelineTechniqueFlags::OctHeading;
    pipelines[3] = pipelines[2];
    pipelines[3].techniques |= simnet::PipelineTechniqueFlags::BitPacking;

    for (auto& pipeline : pipelines)
    {
        pipeline.quantization.position_bounds = simnet::make_centered_bounds(10.0F);
        auto source = make_snapshot(
            1U,
            {{.id = 1U,
              .classification = known_classification,
              .position = {1.2345F, -2.3456F, 3.4567F},
              .heading = simnet::normalize_or({1.0F, 2.0F, 3.0F}, {.x = 1.0F}),
              .hue = 42U}}
        );
        auto encode_state = simnet::ClientReplicationState{};
        auto scratch = simnet::PipelineScratch{};
        auto const full =
            simnet::encode_snapshot(pipeline, encode_state, scratch, {.snapshot = &source});
        source.tick = 2U;
        source.positions[0].x = -4.321F;
        source.headings[0] = simnet::normalize_or({-2.0F, 1.0F, 0.5F}, {.x = 1.0F});
        auto const delta = simnet::encode_snapshot(
            pipeline,
            encode_state,
            scratch,
            {
                .snapshot = &source,
                .baseline_snapshot = &full.resulting_snapshot,
                .baseline_sequence = full.update.sequence,
            }
        );
        auto decode_state = simnet::ClientReplicationState{};
        auto const decoded =
            simnet::decode_update(pipeline, decode_state, {.bytes = delta.update.bytes});
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
        CHECK(same_snapshot_bits(delta.resulting_snapshot, reconstructed));
    }
}

TEST_CASE(
    "field-mask Delta uses semantic masks and writes spawn IDs once",
    "[pipeline][delta][field-mask][wire]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques =
        simnet::PipelineTechniqueFlags::Delta | simnet::PipelineTechniqueFlags::DeltaFieldMask;
    auto const baseline = make_snapshot(
        1U,
        {
            {.id = 1U,
             .classification = known_classification,
             .position = {1.0F, 2.0F, 3.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 10U},
            {.id = 2U,
             .classification = known_classification,
             .position = {2.0F, 3.0F, 4.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 20U},
            {.id = 3U,
             .classification = known_classification,
             .position = {3.0F, 4.0F, 5.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 30U},
            {.id = 4U,
             .classification = known_classification,
             .position = {4.0F, 5.0F, 6.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 40U},
            {.id = 5U,
             .classification = known_classification,
             .position = {5.0F, 6.0F, 7.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 50U},
            {.id = 7U,
             .classification = known_classification,
             .position = {7.0F, 8.0F, 9.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 70U},
        }
    );
    auto current = baseline;
    current.tick = 2U;
    current.classifications[0] = unknown_classification;
    current.positions[1] = {20.0F, 21.0F, 22.0F};
    current.headings[2] = {0.0F, 1.0F, 0.0F};
    current.hues[3] = 44U;
    current.ids[5] = 6U;
    current.positions[5] = {6.0F, 7.0F, 8.0F};
    current.hues[5] = 60U;

    auto encode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const full =
        simnet::encode_snapshot(pipeline, encode_state, scratch, {.snapshot = &baseline});
    REQUIRE(full.kind == simnet::EncodeResultKind::Update);
    CHECK(full.update.bytes.size() == encoded_update_header_bytes + baseline.size() * 30U);

    auto const delta = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {
            .snapshot = &current,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(delta.kind == simnet::EncodeResultKind::Update);
    REQUIRE(delta.report.snapshot_kind == simnet::SnapshotKind::Patch);
    CHECK(delta.report.delta.candidate_count == 6U);
    CHECK(delta.report.delta.unchanged_count == 1U);
    CHECK(delta.report.delta.changed_existing_count == 4U);
    CHECK(delta.report.delta.spawned_count == 1U);
    CHECK(delta.report.delta.masked_existing_upsert_count == 4U);
    CHECK(delta.report.delta.whole_record_existing_upsert_count == 0U);
    CHECK(delta.report.delta.classification_inclusion_count == 1U);
    CHECK(delta.report.delta.position_inclusion_count == 1U);
    CHECK(delta.report.delta.heading_inclusion_count == 1U);
    CHECK(delta.report.delta.hue_inclusion_count == 1U);
    CHECK(delta.report.delta.complete_record_equivalent_bytes == 150U);
    CHECK(delta.report.delta.actual_upsert_representation_bytes == 77U);
    CHECK(delta.update.bytes.size() == encoded_update_header_bytes + 4U + 77U);

    auto constexpr first_upsert = encoded_update_header_bytes + 4U;
    CHECK(std::to_integer<std::uint8_t>(delta.update.bytes[first_upsert + 4U]) == 0x01U);
    CHECK(std::to_integer<std::uint8_t>(delta.update.bytes[first_upsert + 10U]) == 0x02U);
    CHECK(std::to_integer<std::uint8_t>(delta.update.bytes[first_upsert + 27U]) == 0x04U);
    CHECK(std::to_integer<std::uint8_t>(delta.update.bytes[first_upsert + 44U]) == 0x08U);
    CHECK(std::to_integer<std::uint8_t>(delta.update.bytes[first_upsert + 50U]) == 0x80U);

    auto decode_state = simnet::ClientReplicationState{};
    auto const inspected =
        simnet::inspect_encoded_update_header(pipeline, decode_state, delta.update.bytes);
    REQUIRE(inspected.valid());
    CHECK(inspected.sequence == delta.update.sequence);
    CHECK(inspected.baseline_sequence == full.update.sequence);
    CHECK(decode_state.latest_remote_sequence == 0U);
    auto const decoded = simnet::decode_update(
        pipeline,
        decode_state,
        {
            .bytes = delta.update.bytes,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(decoded.report.valid);
    REQUIRE(decoded.update.upserts.size() == 5U);
    auto reconstructed = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(
            &full.resulting_snapshot,
            decoded.update,
            reconstructed
        )
            .valid
    );
    CHECK(same_snapshot_bits(delta.resulting_snapshot, reconstructed));
}

TEST_CASE(
    "field-mask Delta matches whole-record Delta for every representation",
    "[pipeline][delta][field-mask][representation]"
)
{
    auto pipelines = std::array<simnet::PipelineDefinition, 4>{};
    pipelines[0].techniques = simnet::PipelineTechniqueFlags::Delta;
    pipelines[1].techniques =
        simnet::PipelineTechniqueFlags::Delta | simnet::PipelineTechniqueFlags::Quantization;
    pipelines[2] = pipelines[1];
    pipelines[2].techniques |= simnet::PipelineTechniqueFlags::OctHeading;
    pipelines[3] = pipelines[2];
    pipelines[3].techniques |= simnet::PipelineTechniqueFlags::BitPacking;

    for (auto pipeline : pipelines)
    {
        pipeline.quantization.position_bounds = simnet::make_centered_bounds(100.0F);
        auto const baseline = make_snapshot(
            1U,
            {{.id = 1U,
              .classification = known_classification,
              .position = {1.0F, 2.0F, 3.0F},
              .heading = simnet::normalize_or({1.0F, 2.0F, 3.0F}, {.x = 1.0F}),
              .hue = 42U}}
        );
        auto current = baseline;
        current.tick = 2U;
        current.hues[0] = 43U;

        auto whole_state = simnet::ClientReplicationState{};
        auto whole_scratch = simnet::PipelineScratch{};
        auto const whole_full =
            simnet::encode_snapshot(pipeline, whole_state, whole_scratch, {.snapshot = &baseline});
        auto const whole_patch = simnet::encode_snapshot(
            pipeline,
            whole_state,
            whole_scratch,
            {
                .snapshot = &current,
                .baseline_snapshot = &whole_full.resulting_snapshot,
                .baseline_sequence = whole_full.update.sequence,
            }
        );

        pipeline.techniques |= simnet::PipelineTechniqueFlags::DeltaFieldMask;
        auto masked_state = simnet::ClientReplicationState{};
        auto masked_scratch = simnet::PipelineScratch{};
        auto const masked_full = simnet::encode_snapshot(
            pipeline,
            masked_state,
            masked_scratch,
            {.snapshot = &baseline}
        );
        auto const masked_patch = simnet::encode_snapshot(
            pipeline,
            masked_state,
            masked_scratch,
            {
                .snapshot = &current,
                .baseline_snapshot = &masked_full.resulting_snapshot,
                .baseline_sequence = masked_full.update.sequence,
            }
        );
        CHECK(same_snapshot_bits(whole_patch.resulting_snapshot, masked_patch.resulting_snapshot));
        CHECK(masked_patch.update.bytes.size() < whole_patch.update.bytes.size());
        CHECK(
            whole_patch.report.delta.actual_upsert_representation_bytes ==
            whole_patch.report.delta.complete_record_equivalent_bytes
        );
        CHECK(masked_patch.report.delta.actual_upsert_representation_bytes == 6U);
        CHECK(
            masked_patch.report.delta.complete_record_equivalent_bytes ==
            masked_patch.report.representation.record_bytes
        );

        auto decode_state = simnet::ClientReplicationState{};
        auto const decoded = simnet::decode_update_unchecked(
            pipeline,
            decode_state,
            {
                .bytes = masked_patch.update.bytes,
                .baseline_snapshot = &masked_full.resulting_snapshot,
                .baseline_sequence = masked_full.update.sequence,
            }
        );
        REQUIRE(decoded.report.valid);
        CHECK(decoded.update.upserts.front().hue == 43U);

        auto all_fields = baseline;
        all_fields.tick = 3U;
        all_fields.classifications[0] = unknown_classification;
        all_fields.positions[0] = {-9.0F, 8.0F, -7.0F};
        all_fields.headings[0] = {0.0F, 1.0F, 0.0F};
        all_fields.hues[0] = 99U;
        auto const all_fields_patch = simnet::encode_snapshot(
            pipeline,
            masked_state,
            masked_scratch,
            {
                .snapshot = &all_fields,
                .baseline_snapshot = &masked_full.resulting_snapshot,
                .baseline_sequence = masked_full.update.sequence,
            }
        );
        CHECK(
            all_fields_patch.report.delta.actual_upsert_representation_bytes ==
            all_fields_patch.report.delta.complete_record_equivalent_bytes + 1U
        );
    }
}

TEST_CASE(
    "field-mask decode rejects malformed records without state advancement",
    "[pipeline][delta][field-mask][malformed]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques =
        simnet::PipelineTechniqueFlags::Delta | simnet::PipelineTechniqueFlags::DeltaFieldMask;
    auto const baseline = make_snapshot(
        1U,
        {
            {.id = 1U,
             .classification = known_classification,
             .position = {1.0F, 2.0F, 3.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 10U},
            {.id = 2U,
             .classification = known_classification,
             .position = {2.0F, 3.0F, 4.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 20U},
        }
    );
    auto current = baseline;
    current.tick = 2U;
    current.positions[0].x = 9.0F;
    current.hues[1] = 21U;
    auto encode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const full =
        simnet::encode_snapshot(pipeline, encode_state, scratch, {.snapshot = &baseline});
    auto const patch = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {
            .snapshot = &current,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(patch.update.bytes.size() == encoded_update_header_bytes + 23U);

    auto invalid_baseline = full.resulting_snapshot;
    invalid_baseline.headings[0] = {};
    auto checked_state = simnet::ClientReplicationState{};
    auto const checked = simnet::decode_update(
        pipeline,
        checked_state,
        {
            .bytes = patch.update.bytes,
            .baseline_snapshot = &invalid_baseline,
            .baseline_sequence = full.update.sequence,
        }
    );
    CHECK_FALSE(checked.report.valid);
    CHECK(checked.report.error.find("decode baseline is invalid") != std::string::npos);
    CHECK(checked_state.latest_remote_sequence == 0U);
    CHECK(checked.update.empty());

    auto missing_state = simnet::ClientReplicationState{};
    auto const missing =
        simnet::decode_update_unchecked(pipeline, missing_state, {.bytes = patch.update.bytes});
    CHECK_FALSE(missing.report.valid);
    CHECK(missing.report.error.find("exact retained baseline") != std::string::npos);
    CHECK(missing_state.latest_remote_sequence == 0U);

    auto cases = std::vector<std::vector<simnet::Byte>>{};
    auto malformed = patch.update.bytes;
    malformed[encoded_update_header_bytes + 4U] = simnet::Byte{};
    cases.push_back(malformed);
    malformed = patch.update.bytes;
    malformed[encoded_update_header_bytes + 4U] = static_cast<simnet::Byte>(0x10U);
    cases.push_back(malformed);
    malformed = patch.update.bytes;
    write_u32(malformed, encoded_update_header_bytes, 99U);
    cases.push_back(malformed);
    malformed = patch.update.bytes;
    malformed[encoded_update_header_bytes + 4U] = static_cast<simnet::Byte>(0x80U);
    cases.push_back(malformed);
    malformed = patch.update.bytes;
    write_u32(malformed, encoded_update_header_bytes + 17U, 1U);
    cases.push_back(malformed);
    malformed = patch.update.bytes;
    malformed.pop_back();
    write_u32(malformed, 41U, 22U);
    cases.push_back(malformed);
    malformed = patch.update.bytes;
    malformed.push_back(simnet::Byte{0xFFU});
    write_u32(malformed, 41U, 24U);
    cases.push_back(malformed);

    for (auto const& bytes : cases)
    {
        auto state = simnet::ClientReplicationState{};
        auto const decoded = simnet::decode_update_unchecked(
            pipeline,
            state,
            {
                .bytes = bytes,
                .baseline_snapshot = &full.resulting_snapshot,
                .baseline_sequence = full.update.sequence,
            }
        );
        CHECK_FALSE(decoded.report.valid);
        CHECK(decoded.update.empty());
        CHECK(state.latest_remote_sequence == 0U);
    }

    auto delete_current = baseline;
    delete_current.tick = 3U;
    delete_current.ids.pop_back();
    delete_current.classifications.pop_back();
    delete_current.positions.pop_back();
    delete_current.headings.pop_back();
    delete_current.hues.pop_back();
    delete_current.hues[0] = 11U;
    auto overlap_state = simnet::ClientReplicationState{};
    overlap_state.next_sequence = patch.update.sequence + 1U;
    auto overlap_scratch = simnet::PipelineScratch{};
    auto overlap = simnet::encode_snapshot(
        pipeline,
        overlap_state,
        overlap_scratch,
        {
            .snapshot = &delete_current,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(overlap.report.delete_count == 1U);
    REQUIRE(overlap.report.upsert_count == 1U);
    write_u32(overlap.update.bytes, encoded_update_header_bytes, 1U);
    auto overlap_decode_state = simnet::ClientReplicationState{};
    auto const overlap_decoded = simnet::decode_update_unchecked(
        pipeline,
        overlap_decode_state,
        {
            .bytes = overlap.update.bytes,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    CHECK_FALSE(overlap_decoded.report.valid);
    CHECK(overlap_decoded.update.empty());
    CHECK(overlap_decode_state.latest_remote_sequence == 0U);

    auto whole_pipeline = pipeline;
    whole_pipeline.techniques = simnet::PipelineTechniqueFlags::Delta;
    auto incompatible_state = simnet::ClientReplicationState{};
    auto const incompatible = simnet::inspect_encoded_update_header(
        whole_pipeline,
        incompatible_state,
        patch.update.bytes
    );
    CHECK_FALSE(incompatible.valid());
    CHECK(
        (incompatible.error == simnet::EncodedUpdateHeaderError::UnsupportedVersion ||
         incompatible.error == simnet::EncodedUpdateHeaderError::SignatureMismatch)
    );
    CHECK(incompatible_state.latest_remote_sequence == 0U);

    auto stale_state = simnet::ClientReplicationState{};
    stale_state.latest_remote_sequence = patch.update.sequence;
    auto const stale =
        simnet::inspect_encoded_update_header(pipeline, stale_state, patch.update.bytes);
    CHECK(stale.error == simnet::EncodedUpdateHeaderError::StaleSequence);
    CHECK(stale_state.latest_remote_sequence == patch.update.sequence);
}

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    make_snapshot(simnet::Tick tick, std::vector<simnet::EntityState> const& entities)
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

    [[nodiscard]] simnet::WorldSnapshot make_linear_snapshot(simnet::Tick tick, std::uint32_t count)
    {
        auto entities = std::vector<simnet::EntityState>{};
        entities.reserve(count);
        for (auto index = std::uint32_t{}; index < count; ++index)
        {
            entities.push_back({
                .id = index + 1U,
                .classification = known_classification,
                .position = {static_cast<float>(index), 0.0F, 0.0F},
                .heading = {1.0F, 0.0F, 0.0F},
                .hue = static_cast<std::uint8_t>(index),
            });
        }
        return make_snapshot(tick, entities);
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

    [[nodiscard]] simnet::WorldSnapshot reconstruct(
        simnet::WorldSnapshot const* baseline,
        simnet::DecodeOutput const& decoded
    )
    {
        auto result = simnet::WorldSnapshot{};
        REQUIRE(
            simnet::reconstruct_world_snapshot_unchecked(baseline, decoded.update, result).valid
        );
        return result;
    }
}

TEST_CASE("reference FullReplace codec has the documented fixed-width wire contract", "[pipeline][reference]")
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
    auto scratch = simnet::PipelineScratch{};
    auto const encoded =
        simnet::encode_snapshot(pipeline, encode_state, scratch, {.snapshot = &snapshot});

    REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
    CHECK(encoded.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    CHECK(encoded.report.representation.record_bytes == 30U);
    CHECK(encoded.update.bytes.size() == encoded_update_header_bytes + 30U);

    CHECK(encoded.update.bytes[0] == simnet::Byte{0x53U});
    CHECK(encoded.update.bytes[1] == simnet::Byte{0x4EU});
    CHECK(encoded.update.bytes[2] == simnet::Byte{0x50U});
    CHECK(encoded.update.bytes[3] == simnet::Byte{0x4CU});
    CHECK(encoded.update.bytes[6] == simnet::Byte{0U});
    CHECK(encoded.update.bytes[7] == simnet::Byte{5U});

    auto decode_state = simnet::ClientReplicationState{};
    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});
    REQUIRE(decoded.report.valid);

    auto const reconstructed = reconstruct(nullptr, decoded);
    CHECK(same_snapshot_bits(reconstructed, snapshot));
}

TEST_CASE(
    "representation treatments report their widths and reconstruct their canonical Client state",
    "[pipeline][representation][quality]"
)
{
    auto const source = make_snapshot(
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
        auto encode_state = simnet::ClientReplicationState{};
        auto scratch = simnet::PipelineScratch{};
        outputs[index] = simnet::encode_snapshot(
            pipelines[index],
            encode_state,
            scratch,
            {.snapshot = &source}
        );
        outputs[index].report.representation = simnet::measure_representation_quality(
            pipelines[index],
            source,
            scratch.logical_update
        );

        auto const& report = outputs[index].report.representation;
        CHECK(report.record_bytes == expected_widths[index]);
        CHECK(report.quality_sample_count == source.size());
        CHECK(std::isfinite(report.position_error_sum));
        CHECK(std::isfinite(report.position_error_maximum));
        CHECK(std::isfinite(report.heading_angular_error_degrees_sum));
        CHECK(std::isfinite(report.heading_angular_error_degrees_maximum));

        auto decode_state = simnet::ClientReplicationState{};
        auto const decoded = simnet::decode_update(
            pipelines[index],
            decode_state,
            {.bytes = outputs[index].update.bytes}
        );
        REQUIRE(decoded.report.valid);
        CHECK(same_snapshot_bits(reconstruct(nullptr, decoded), outputs[index].resulting_snapshot));
    }

    CHECK(outputs[0].report.representation.position_error_sum == 0.0);
    CHECK(outputs[0].report.representation.heading_angular_error_degrees_sum == 0.0);
    CHECK(outputs[1].report.representation.position_error_sum > 0.0);
    CHECK(outputs[1].report.representation.position_error_maximum < 0.001);
    CHECK(
        outputs[2].report.representation.position_error_sum ==
        outputs[3].report.representation.position_error_sum
    );
    CHECK(
        outputs[2].report.representation.heading_angular_error_degrees_sum ==
        outputs[3].report.representation.heading_angular_error_degrees_sum
    );

    auto no_quality_state = simnet::ClientReplicationState{};
    auto no_quality_scratch = simnet::PipelineScratch{};
    auto const no_quality = simnet::encode_snapshot(
        pipelines[3],
        no_quality_state,
        no_quality_scratch,
        {.snapshot = &source}
    );
    CHECK(no_quality.update.bytes == outputs[3].update.bytes);
    CHECK(no_quality.report.representation.quality_sample_count == 0U);
}

TEST_CASE("Delta emits changed spawned and deleted entities against the exact baseline", "[pipeline][delta]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Delta;

    auto const baseline = make_snapshot(
        1U,
        {
            {.id = 1U,
             .classification = known_classification,
             .position = {1.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 10U},
            {.id = 2U,
             .classification = known_classification,
             .position = {2.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 20U},
            {.id = 3U,
             .classification = known_classification,
             .position = {3.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 30U},
        }
    );
    auto const current = make_snapshot(
        2U,
        {
            {.id = 1U,
             .classification = known_classification,
             .position = {1.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 10U},
            {.id = 2U,
             .classification = unknown_classification,
             .position = {20.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 20U},
            {.id = 4U,
             .classification = known_classification,
             .position = {4.0F, 0.0F, 0.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 40U},
        }
    );

    auto encode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const full =
        simnet::encode_snapshot(pipeline, encode_state, scratch, {.snapshot = &baseline});

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

    REQUIRE(delta.report.snapshot_kind == simnet::SnapshotKind::Patch);
    CHECK(delta.report.baseline_sequence == full.update.sequence);
    CHECK(delta.report.delta.candidate_count == 3U);
    CHECK(delta.report.delta.unchanged_count == 1U);
    CHECK(delta.report.delta.changed_existing_count == 1U);
    CHECK(delta.report.delta.spawned_count == 1U);
    CHECK(delta.report.upsert_count == 2U);
    CHECK(delta.report.delete_count == 1U);

    auto decode_state = simnet::ClientReplicationState{};
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
    CHECK(decoded.update.deletes == std::vector<simnet::EntityNetId>{3U});
    CHECK(same_snapshot_bits(reconstruct(&full.resulting_snapshot, decoded), delta.resulting_snapshot));
}

TEST_CASE("quantized Delta compares canonical values rather than source floats", "[pipeline][delta][quantized]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques =
        simnet::PipelineTechniqueFlags::Delta | simnet::PipelineTechniqueFlags::Quantization;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(10.0F);

    auto source = make_snapshot(
        1U,
        {
            {.id = 1U,
             .classification = known_classification,
             .position = {},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 4U},
        }
    );

    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const full = simnet::encode_snapshot(pipeline, state, scratch, {.snapshot = &source});

    source.tick = 2U;
    source.positions[0].x = 0.0001F;
    source.headings[0] = simnet::normalize_or({1.0F, 0.000001F, 0.0F}, {.x = 1.0F});
    auto const sub_quantum = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    CHECK(sub_quantum.report.delta.unchanged_count == 1U);
    CHECK(sub_quantum.report.upsert_count == 0U);

    source.tick = 3U;
    source.positions[0].x = 0.001F;
    auto const adjacent_bin = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    CHECK(adjacent_bin.report.delta.changed_existing_count == 1U);
    CHECK(adjacent_bin.report.upsert_count == 1U);
}

TEST_CASE("field-mask Delta sends only changed fields and reconstructs exactly", "[pipeline][delta][field-mask]")
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

    REQUIRE(delta.report.snapshot_kind == simnet::SnapshotKind::Patch);
    CHECK(delta.report.delta.changed_existing_count == 4U);
    CHECK(delta.report.delta.spawned_count == 1U);
    CHECK(delta.report.delta.masked_existing_upsert_count == 4U);
    CHECK(delta.report.delta.classification_inclusion_count == 1U);
    CHECK(delta.report.delta.position_inclusion_count == 1U);
    CHECK(delta.report.delta.heading_inclusion_count == 1U);
    CHECK(delta.report.delta.hue_inclusion_count == 1U);
    CHECK(delta.report.delta.complete_record_equivalent_bytes == 150U);
    CHECK(delta.report.delta.actual_upsert_representation_bytes == 77U);
    CHECK(delta.report.delta.actual_upsert_representation_bytes <
          delta.report.delta.complete_record_equivalent_bytes);

    auto decode_state = simnet::ClientReplicationState{};
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
    CHECK(same_snapshot_bits(reconstruct(&full.resulting_snapshot, decoded), delta.resulting_snapshot));
}

TEST_CASE("Incremental patches eventually converge removals and spawns", "[pipeline][incremental]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Incremental;
    pipeline.incremental.max_entities_per_update = 2U;

    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto source = make_linear_snapshot(1U, 3U);

    auto first = simnet::encode_snapshot(pipeline, state, scratch, {.snapshot = &source});
    REQUIRE(first.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    CHECK(first.resulting_snapshot.ids == std::vector<simnet::EntityNetId>{1U, 2U, 3U});

    auto replica = first.resulting_snapshot;
    auto replica_sequence = first.update.sequence;

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
        {
            .snapshot = &source,
            .replica_snapshot = &replica,
            .replica_sequence = replica_sequence,
        }
    );
    REQUIRE(second.report.snapshot_kind == simnet::SnapshotKind::Patch);
    CHECK(second.report.delete_count == 1U);
    CHECK(scratch.logical_update.deletes == std::vector<simnet::EntityNetId>{2U});
    CHECK(second.resulting_snapshot.ids == std::vector<simnet::EntityNetId>{1U, 3U});

    replica = second.resulting_snapshot;
    replica_sequence = second.update.sequence;
    source.tick = 3U;

    auto third = simnet::encode_snapshot(
        pipeline,
        state,
        scratch,
        {
            .snapshot = &source,
            .replica_snapshot = &replica,
            .replica_sequence = replica_sequence,
        }
    );
    CHECK(third.resulting_snapshot.ids == std::vector<simnet::EntityNetId>{1U, 3U, 4U});
    CHECK(third.resulting_snapshot.ids == source.ids);
}

TEST_CASE("send interval emits the configured deterministic cadence", "[pipeline][cadence]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::SendInterval;
    pipeline.send_interval.interval_ticks = 3U;

    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const expected = std::array{
        simnet::EncodeResultKind::Update,
        simnet::EncodeResultKind::Skipped,
        simnet::EncodeResultKind::Skipped,
        simnet::EncodeResultKind::Update,
        simnet::EncodeResultKind::Skipped,
        simnet::EncodeResultKind::Skipped,
        simnet::EncodeResultKind::Update,
    };

    for (auto tick = simnet::Tick{}; tick < expected.size(); ++tick)
    {
        auto const source = make_linear_snapshot(tick, 1U);
        auto const encoded =
            simnet::encode_snapshot(pipeline, state, scratch, {.snapshot = &source});
        CHECK(encoded.kind == expected[tick]);
        CHECK(
            simnet::should_emit_snapshot(pipeline, tick) ==
            (expected[tick] == simnet::EncodeResultKind::Update)
        );
    }

    CHECK(state.next_sequence == 4U);
}

TEST_CASE(
    "Incremental Delta representation techniques compose and reconstruct each Patch",
    "[pipeline][incremental][delta][representation]"
)
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques =
        simnet::PipelineTechniqueFlags::Incremental | simnet::PipelineTechniqueFlags::Delta |
        simnet::PipelineTechniqueFlags::DeltaFieldMask | simnet::PipelineTechniqueFlags::Quantization |
        simnet::PipelineTechniqueFlags::OctHeading | simnet::PipelineTechniqueFlags::BitPacking;
    pipeline.incremental.max_entities_per_update = 3U;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(100.0F);

    auto const baseline = make_linear_snapshot(10U, 6U);
    auto current = make_linear_snapshot(11U, 5U);
    current.positions[1].x = 20.0F;
    current.classifications[2] = unknown_classification;
    current.positions[3].x = 40.0F;
    current.ids.push_back(7U);
    current.classifications.push_back(known_classification);
    current.positions.push_back({60.0F, 0.0F, 0.0F});
    current.headings.push_back({1.0F, 0.0F, 0.0F});
    current.hues.push_back(6U);

    auto encode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const full =
        simnet::encode_snapshot(pipeline, encode_state, scratch, {.snapshot = &baseline});
    REQUIRE(full.report.snapshot_kind == simnet::SnapshotKind::FullReplace);

    auto first = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {
            .snapshot = &current,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(first.report.snapshot_kind == simnet::SnapshotKind::Patch);
    CHECK(first.report.delete_count == 1U);

    auto first_decode_state = simnet::ClientReplicationState{};
    auto const first_decoded = simnet::decode_update(
        pipeline,
        first_decode_state,
        {
            .bytes = first.update.bytes,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(first_decoded.report.valid);
    CHECK(same_snapshot_bits(reconstruct(&full.resulting_snapshot, first_decoded), first.resulting_snapshot));

    auto second = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {
            .snapshot = &current,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );

    auto second_decode_state = simnet::ClientReplicationState{};
    auto const second_decoded = simnet::decode_update(
        pipeline,
        second_decode_state,
        {
            .bytes = second.update.bytes,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(second_decoded.report.valid);
    CHECK(same_snapshot_bits(reconstruct(&full.resulting_snapshot, second_decoded), second.resulting_snapshot));
    CHECK(second.resulting_snapshot.ids == std::vector<simnet::EntityNetId>{1U, 2U, 3U, 4U, 5U, 7U});
}

TEST_CASE("malformed encoded updates are rejected without sequence advancement", "[pipeline][malformed]")
{
    auto const pipeline = simnet::PipelineDefinition{};
    auto const source = make_linear_snapshot(1U, 1U);

    auto encode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const encoded =
        simnet::encode_snapshot(pipeline, encode_state, scratch, {.snapshot = &source});

    auto truncated = encoded.update.bytes;
    truncated.pop_back();

    auto decode_state = simnet::ClientReplicationState{};
    auto const rejected = simnet::decode_update(pipeline, decode_state, {.bytes = truncated});
    CHECK_FALSE(rejected.report.valid);
    CHECK(rejected.update.empty());
    CHECK(decode_state.latest_remote_sequence == 0U);

    auto incompatible = simnet::PipelineDefinition{};
    incompatible.techniques = simnet::PipelineTechniqueFlags::Quantization;
    auto incompatible_state = simnet::ClientReplicationState{};
    auto const wrong_signature =
        simnet::decode_update(incompatible, incompatible_state, {.bytes = encoded.update.bytes});
    CHECK_FALSE(wrong_signature.report.valid);
    CHECK(incompatible_state.latest_remote_sequence == 0U);
}

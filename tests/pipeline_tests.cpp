#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

import simnet.core;
import simnet.pipeline;
import simnet.snapshot;

namespace
{
    [[nodiscard]] simnet::WorldSnapshot
    make_snapshot(simnet::Tick tick, std::vector<simnet::EntityState> const& boids)
    {
        auto snapshot = simnet::WorldSnapshot{};
        snapshot.tick = tick;
        snapshot.reserve(boids.size());
        for (auto const& boid : boids) {
            snapshot.ids.push_back(boid.id);
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
        for (std::uint32_t index = 0; index < count; ++index) {
            boids.push_back({
                .id = static_cast<simnet::EntityNetId>(index + 1U),
                .position = {static_cast<float>(index), 0.0F, 0.0F},
                .heading = {1.0F, 0.0F, 0.0F},
                .hue = static_cast<std::uint8_t>(index),
            });
        }
        return make_snapshot(tick, boids);
    }
}

TEST_CASE("delta pipeline preserves baseline and patch semantics", "[pipeline][delta]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;

    auto const baseline = make_snapshot(
        0,
        {
            {.id = 1, .position = {1.0F, 0.0F, 0.0F}, .heading = {1.0F, 0.0F, 0.0F}, .hue = 10},
            {.id = 2, .position = {2.0F, 0.0F, 0.0F}, .heading = {1.0F, 0.0F, 0.0F}, .hue = 20},
            {.id = 3, .position = {3.0F, 0.0F, 0.0F}, .heading = {1.0F, 0.0F, 0.0F}, .hue = 30},
        }
    );
    auto const current = make_snapshot(
        1,
        {
            {.id = 1, .position = {1.0F, 0.0F, 0.0F}, .heading = {1.0F, 0.0F, 0.0F}, .hue = 10},
            {.id = 2, .position = {20.0F, 0.0F, 0.0F}, .heading = {1.0F, 0.0F, 0.0F}, .hue = 20},
            {.id = 4, .position = {4.0F, 0.0F, 0.0F}, .heading = {1.0F, 0.0F, 0.0F}, .hue = 40},
        }
    );

    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto decode_scratch = simnet::PipelineScratch{};

    auto const full
        = simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &baseline});
    REQUIRE(full.kind == simnet::EncodeResultKind::Update);
    REQUIRE(full.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    REQUIRE(full.report.baseline_sequence == 0);

    auto const full_decoded = simnet::decode_update(
        pipeline,
        decode_state,
        decode_scratch,
        {.bytes = full.update.bytes}
    );
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
    REQUIRE(delta.report.delta);
    REQUIRE(delta.report.snapshot_kind == simnet::SnapshotKind::Patch);
    REQUIRE(delta.report.baseline_sequence == full.update.sequence);
    REQUIRE(delta.report.upsert_count == 2);
    REQUIRE(delta.report.delete_count == 1);

    auto const decoded = simnet::decode_update(
        pipeline,
        decode_state,
        decode_scratch,
        {.bytes = delta.update.bytes}
    );
    REQUIRE(decoded.report.valid);
    REQUIRE(decoded.update.upserts.size() == 2);
    CHECK(decoded.update.upserts[0].id == 2);
    CHECK(decoded.update.upserts[1].id == 4);
    CHECK(decoded.update.deletes == std::vector<simnet::EntityNetId>{3});
}

TEST_CASE("deltas reconstruct from their exact retained baseline", "[pipeline][delta][replication]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;

    auto const baseline = make_snapshot(
        10,
        {
            {.id = 1, .position = {1.0F, 0.0F, 0.0F}, .heading = {1.0F, 0.0F, 0.0F}, .hue = 10},
            {.id = 2, .position = {2.0F, 0.0F, 0.0F}, .heading = {1.0F, 0.0F, 0.0F}, .hue = 20},
        }
    );
    auto const tick_11 = make_snapshot(
        11,
        {
            {.id = 1, .position = {11.0F, 0.0F, 0.0F}, .heading = {1.0F, 0.0F, 0.0F}, .hue = 10},
            {.id = 2, .position = {2.0F, 0.0F, 0.0F}, .heading = {1.0F, 0.0F, 0.0F}, .hue = 20},
        }
    );
    auto tick_12 = baseline;
    tick_12.tick = 12;

    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto decode_scratch = simnet::PipelineScratch{};

    auto const full
        = simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &baseline});
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

    auto const decoded_full = simnet::decode_update(
        pipeline,
        decode_state,
        decode_scratch,
        {.bytes = full.update.bytes}
    );
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

    auto const decoded_first = simnet::decode_update(
        pipeline,
        decode_state,
        decode_scratch,
        {.bytes = first_delta.update.bytes}
    );
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

    auto const decoded_second = simnet::decode_update(
        pipeline,
        decode_state,
        decode_scratch,
        {.bytes = second_delta.update.bytes}
    );
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

TEST_CASE("pipeline validation rejects unsupported technique combinations", "[pipeline]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Incremental;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Quantization;

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
    auto encoded
        = simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &snapshot});
    REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
    REQUIRE(encoded.report.payload_bytes >= sizeof(simnet::EntityNetId));

    auto const payload_offset = encoded.update.bytes.size() - encoded.report.payload_bytes;
    for (std::size_t offset = 0; offset < sizeof(simnet::EntityNetId); ++offset) {
        encoded.update.bytes[payload_offset + offset] = simnet::Byte{};
    }

    auto decode_state = simnet::ClientReplicationState{};
    auto decode_scratch = simnet::PipelineScratch{};
    auto const decoded = simnet::decode_update(
        pipeline,
        decode_state,
        decode_scratch,
        {.bytes = encoded.update.bytes}
    );

    CHECK_FALSE(decoded.report.valid);
    CHECK(decoded.report.error.find("entity id zero is reserved") != std::string::npos);
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
    auto const full
        = simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &baseline});
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
    REQUIRE(encoded.report.payload_bytes >= sizeof(simnet::EntityNetId));

    auto const payload_offset = encoded.update.bytes.size() - encoded.report.payload_bytes;
    for (std::size_t offset = 0; offset < sizeof(simnet::EntityNetId); ++offset) {
        encoded.update.bytes[payload_offset + offset] = simnet::Byte{};
    }

    auto decode_state = simnet::ClientReplicationState{};
    auto decode_scratch = simnet::PipelineScratch{};
    auto const decoded = simnet::decode_update(
        pipeline,
        decode_state,
        decode_scratch,
        {.bytes = encoded.update.bytes}
    );

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
    auto const full
        = simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &baseline});
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
    for (std::size_t offset = 0; offset < sizeof(simnet::SequenceId); ++offset) {
        encoded.update.bytes[baseline_sequence_offset + offset] = simnet::Byte{};
    }

    auto decode_state = simnet::ClientReplicationState{};
    auto decode_scratch = simnet::PipelineScratch{};
    auto const decoded = simnet::decode_update(
        pipeline,
        decode_state,
        decode_scratch,
        {.bytes = encoded.update.bytes}
    );

    CHECK_FALSE(decoded.report.valid);
    CHECK(
        decoded.report.error
        == "decoded update is invalid: full replacement snapshot update deletes must be empty"
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
    "incremental pipeline advances cursor only on scheduled emissions",
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
    auto decode_scratch = simnet::PipelineScratch{};
    auto emitted_index = std::size_t{};
    auto const expected_ids = std::array{
        std::vector<simnet::EntityNetId>{1, 2, 3, 4},
        std::vector<simnet::EntityNetId>{5, 6, 7, 8},
        std::vector<simnet::EntityNetId>{1, 2, 9, 10},
    };

    for (simnet::Tick tick = 0; tick < 5; ++tick) {
        auto const snapshot = make_linear_snapshot(tick, 10);
        auto const encoded = simnet::encode_snapshot(
            pipeline,
            encode_state,
            encode_scratch,
            {.snapshot = &snapshot}
        );

        if ((tick % 2U) != 0U) {
            CHECK(encoded.kind == simnet::EncodeResultKind::Skipped);
            CHECK(encoded.skip_reason == simnet::EncodeSkipReason::SendInterval);
            continue;
        }

        REQUIRE(emitted_index < expected_ids.size());
        REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
        CHECK(encoded.update.sequence == emitted_index + 1U);
        CHECK(encoded.report.snapshot_kind == simnet::SnapshotKind::Patch);
        CHECK(encoded.report.selected_entities == 4);

        auto const decoded = simnet::decode_update(
            pipeline,
            decode_state,
            decode_scratch,
            {.bytes = encoded.update.bytes}
        );
        REQUIRE(decoded.report.valid);

        auto ids = std::vector<simnet::EntityNetId>{};
        ids.reserve(decoded.update.upserts.size());
        for (auto const& boid : decoded.update.upserts) {
            ids.push_back(boid.id);
        }
        CHECK(ids == expected_ids[emitted_index]);
        ++emitted_index;
    }

    CHECK(emitted_index == expected_ids.size());
    CHECK(encode_state.incremental_cursor == 2);
    CHECK(encode_state.next_sequence == 4);
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
             .position = {-100.0F, 0.0F, 100.0F},
             .heading = {1.0F, 0.0F, 0.0F},
             .hue = 10},
            {.id = 2, .position = {0.0F, 25.0F, -50.0F}, .heading = {0.0F, 1.0F, 0.0F}, .hue = 20},
            {.id = 3,
             .position = {100.0F, -100.0F, 0.0F},
             .heading = {0.0F, 0.0F, 1.0F},
             .hue = 30},
        }
    );
    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto decode_scratch = simnet::PipelineScratch{};

    auto const encoded
        = simnet::encode_snapshot(pipeline, encode_state, encode_scratch, {.snapshot = &snapshot});
    REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
    CHECK(encoded.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    CHECK(encoded.report.upsert_count == 3);
    CHECK(encoded.report.payload_bytes == 45);

    auto const decoded = simnet::decode_update(
        pipeline,
        decode_state,
        decode_scratch,
        {.bytes = encoded.update.bytes}
    );
    REQUIRE(decoded.report.valid);
    REQUIRE(decoded.update.upserts.size() == 3);
    CHECK(decoded.update.tick == 7);
    CHECK(decoded.update.upserts[0].id == 1);
    CHECK(decoded.update.upserts[1].id == 2);
    CHECK(decoded.update.upserts[2].id == 3);
    CHECK(simnet::validate_client_snapshot_patch(decoded.update).valid);
}

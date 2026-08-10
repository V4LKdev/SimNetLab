#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

import simnet.compression;
import simnet.core;
import simnet.packetization;
import simnet.pipeline;
import simnet.snapshot;
import simnet.synthetic;

namespace
{
    [[nodiscard]] bool same_vec3_bits(simnet::Vec3f left, simnet::Vec3f right) noexcept
    {
        return std::bit_cast<std::uint32_t>(left.x) == std::bit_cast<std::uint32_t>(right.x) &&
               std::bit_cast<std::uint32_t>(left.y) == std::bit_cast<std::uint32_t>(right.y) &&
               std::bit_cast<std::uint32_t>(left.z) == std::bit_cast<std::uint32_t>(right.z);
    }

    [[nodiscard]] bool same_snapshot_bits(
        simnet::WorldSnapshot const& left,
        simnet::WorldSnapshot const& right
    ) noexcept
    {
        return left.tick == right.tick && left.ids == right.ids &&
               left.classifications == right.classifications && left.hues == right.hues &&
               left.positions.size() == right.positions.size() &&
               left.headings.size() == right.headings.size() &&
               std::equal(
                   left.positions.begin(),
                   left.positions.end(),
                   right.positions.begin(),
                   same_vec3_bits
               ) &&
               std::equal(
                   left.headings.begin(),
                   left.headings.end(),
                   right.headings.begin(),
                   same_vec3_bits
               );
    }

    [[nodiscard]] simnet::SyntheticSnapshotSettings settings(std::uint32_t count = 10U)
    {
        return {
            .seed = 12345U,
            .entity_count = count,
            .bounds = simnet::make_centered_bounds(220.0F),
            .pattern = simnet::SyntheticPattern::RandomUniform,
        };
    }

    void check_entity_equal(
        simnet::WorldSnapshot const& left,
        simnet::WorldSnapshot const& right,
        std::size_t index
    )
    {
        CHECK(left.ids[index] == right.ids[index]);
        CHECK(left.classifications[index] == right.classifications[index]);
        CHECK(same_vec3_bits(left.positions[index], right.positions[index]));
        CHECK(same_vec3_bits(left.headings[index], right.headings[index]));
        CHECK(left.hues[index] == right.hues[index]);
    }
}

TEST_CASE("synthetic snapshots use deterministic nonzero entity ids", "[synthetic][snapshot]")
{
    auto const snapshot = simnet::make_synthetic_world_snapshot(settings(), 7U);
    REQUIRE(snapshot.size() == 10U);
    CHECK(snapshot.ids.front() == 1U);
    CHECK(snapshot.ids.back() == 10U);
    CHECK(std::is_sorted(snapshot.ids.begin(), snapshot.ids.end()));
}

TEST_CASE(
    "stateful default generation preserves legacy snapshots from any first tick",
    "[synthetic][snapshot][compatibility]"
)
{
    auto const snapshot_settings = settings(32U);
    auto const change_settings = simnet::SyntheticChangeSettings{};
    auto first = simnet::SyntheticSnapshotState{};
    auto second = simnet::SyntheticSnapshotState{};

    for (simnet::Tick tick = 37U; tick != 43U; ++tick)
    {
        auto const& first_snapshot = simnet::update_synthetic_world_snapshot(
            snapshot_settings,
            change_settings,
            tick,
            first
        );
        auto const& second_snapshot = simnet::update_synthetic_world_snapshot(
            snapshot_settings,
            change_settings,
            tick,
            second
        );
        auto const legacy = simnet::make_synthetic_world_snapshot(snapshot_settings, tick);
        CHECK(same_snapshot_bits(first_snapshot, legacy));
        CHECK(same_snapshot_bits(first_snapshot, second_snapshot));
    }
}

TEST_CASE(
    "synthetic cohorts have exact counts and rotate fairly in ascending id order",
    "[synthetic][snapshot][cohort]"
)
{
    auto const snapshot_settings = settings();
    auto const changes = simnet::SyntheticChangeSettings{
        .entity_change_fraction = 0.25,
        .field_change_mode = simnet::SyntheticFieldChangeMode::All,
    };
    auto state = simnet::SyntheticSnapshotState{};
    static_cast<void>(
        simnet::update_synthetic_world_snapshot(snapshot_settings, changes, 7U, state)
    );

    for (auto step = std::uint32_t{}; step < 5U; ++step)
    {
        auto const previous = state.current;
        auto const tick = static_cast<simnet::Tick>(8U + step);
        auto const candidate = simnet::make_synthetic_world_snapshot(snapshot_settings, tick);
        auto const first_index = static_cast<std::uint32_t>((step * 2U) % 10U);
        auto const& current =
            simnet::update_synthetic_world_snapshot(snapshot_settings, changes, tick, state);
        CHECK(state.next_entity_index == ((first_index + 2U) % 10U));
        for (auto index = std::uint32_t{}; index < 10U; ++index)
        {
            auto const serviced = index == first_index || index == ((first_index + 1U) % 10U);
            check_entity_equal(current, serviced ? candidate : previous, index);
        }
    }

    auto one_state = simnet::SyntheticSnapshotState{};
    auto const one_changes = simnet::SyntheticChangeSettings{
        .entity_change_fraction = 0.001,
        .field_change_mode = simnet::SyntheticFieldChangeMode::All,
    };
    static_cast<void>(
        simnet::update_synthetic_world_snapshot(snapshot_settings, one_changes, 20U, one_state)
    );
    static_cast<void>(
        simnet::update_synthetic_world_snapshot(snapshot_settings, one_changes, 21U, one_state)
    );
    CHECK(one_state.next_entity_index == 1U);

    auto frozen = simnet::SyntheticSnapshotState{};
    auto const no_changes = simnet::SyntheticChangeSettings{
        .entity_change_fraction = 0.0,
        .field_change_mode = simnet::SyntheticFieldChangeMode::All,
    };
    auto const initial =
        simnet::update_synthetic_world_snapshot(snapshot_settings, no_changes, 30U, frozen);
    auto expected = initial;
    expected.tick = 31U;
    auto const& unchanged =
        simnet::update_synthetic_world_snapshot(snapshot_settings, no_changes, 31U, frozen);
    CHECK(same_snapshot_bits(unchanged, expected));
    CHECK(frozen.next_entity_index == 0U);
}

TEST_CASE("synthetic field modes change only their named canonical groups", "[synthetic][fields]")
{
    auto const snapshot_settings = settings(8U);
    for (auto const mode : {
             simnet::SyntheticFieldChangeMode::All,
             simnet::SyntheticFieldChangeMode::Transform,
             simnet::SyntheticFieldChangeMode::PositionOnly,
             simnet::SyntheticFieldChangeMode::HeadingOnly,
         })
    {
        CAPTURE(static_cast<std::uint32_t>(mode));
        auto state = simnet::SyntheticSnapshotState{};
        auto const changes = simnet::SyntheticChangeSettings{
            .entity_change_fraction = 0.25,
            .field_change_mode = mode,
        };
        auto const initial =
            simnet::update_synthetic_world_snapshot(snapshot_settings, changes, 50U, state);
        auto const previous = initial;
        auto const candidate = simnet::make_synthetic_world_snapshot(snapshot_settings, 51U);
        auto const& current =
            simnet::update_synthetic_world_snapshot(snapshot_settings, changes, 51U, state);

        CHECK(current.ids == previous.ids);
        CHECK(current.classifications == previous.classifications);
        for (auto index = std::size_t{}; index < current.size(); ++index)
        {
            auto const serviced = index < 2U;
            auto const position_changes =
                serviced && mode != simnet::SyntheticFieldChangeMode::HeadingOnly;
            auto const heading_changes =
                serviced && mode != simnet::SyntheticFieldChangeMode::PositionOnly;
            auto const hue_changes = serviced && mode == simnet::SyntheticFieldChangeMode::All;
            CHECK(same_vec3_bits(
                current.positions[index],
                position_changes ? candidate.positions[index] : previous.positions[index]
            ));
            CHECK(same_vec3_bits(
                current.headings[index],
                heading_changes ? candidate.headings[index] : previous.headings[index]
            ));
            CHECK(
                current.hues[index] == (hue_changes ? candidate.hues[index] : previous.hues[index])
            );
        }
    }
}

TEST_CASE("synthetic sequence rejection is transactional", "[synthetic][transaction]")
{
    auto const snapshot_settings = settings();
    auto const changes = simnet::SyntheticChangeSettings{
        .entity_change_fraction = 0.5,
        .field_change_mode = simnet::SyntheticFieldChangeMode::PositionOnly,
    };
    auto state = simnet::SyntheticSnapshotState{};
    static_cast<void>(
        simnet::update_synthetic_world_snapshot(snapshot_settings, changes, 90U, state)
    );
    auto const before = state;

    CHECK_THROWS(simnet::update_synthetic_world_snapshot(snapshot_settings, changes, 92U, state));
    CHECK(same_snapshot_bits(state.current, before.current));
    CHECK(same_snapshot_bits(state.candidate, before.candidate));
    CHECK(state.next_entity_index == before.next_entity_index);
    CHECK(state.last_accepted_tick == before.last_accepted_tick);

    auto incompatible = changes;
    incompatible.entity_change_fraction = 0.25;
    CHECK_THROWS(
        simnet::update_synthetic_world_snapshot(snapshot_settings, incompatible, 91U, state)
    );
    CHECK(same_snapshot_bits(state.current, before.current));
    CHECK(same_snapshot_bits(state.candidate, before.candidate));
    CHECK(state.next_entity_index == before.next_entity_index);
    CHECK(state.last_accepted_tick == before.last_accepted_tick);

    auto terminal = simnet::SyntheticSnapshotState{};
    static_cast<void>(simnet::update_synthetic_world_snapshot(
        snapshot_settings,
        changes,
        std::numeric_limits<simnet::Tick>::max(),
        terminal
    ));
    CHECK_THROWS(
        simnet::update_synthetic_world_snapshot(
            snapshot_settings,
            changes,
            std::numeric_limits<simnet::Tick>::max(),
            terminal
        )
    );
}

TEST_CASE(
    "sparse synthetic updates compose through Delta compression and packetization",
    "[synthetic][pipeline][delta][compression][packetization]"
)
{
    auto const snapshot_settings = settings(32U);
    auto const changes = simnet::SyntheticChangeSettings{
        .entity_change_fraction = 0.125,
        .field_change_mode = simnet::SyntheticFieldChangeMode::PositionOnly,
    };
    auto synthetic_state = simnet::SyntheticSnapshotState{};
    auto const& initial =
        simnet::update_synthetic_world_snapshot(snapshot_settings, changes, 100U, synthetic_state);

    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques =
        simnet::PipelineTechniqueFlags::Delta | simnet::PipelineTechniqueFlags::DeltaFieldMask |
        simnet::PipelineTechniqueFlags::Quantization | simnet::PipelineTechniqueFlags::OctHeading |
        simnet::PipelineTechniqueFlags::BitPacking;
    pipeline.quantization.position_bounds = snapshot_settings.bounds;
    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const full =
        simnet::encode_snapshot(pipeline, encode_state, scratch, {.snapshot = &initial});
    REQUIRE(full.kind == simnet::EncodeResultKind::Update);
    auto const decoded_full =
        simnet::decode_update(pipeline, decode_state, {.bytes = full.update.bytes});
    REQUIRE(decoded_full.report.valid);
    auto reconstructed_full = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot(nullptr, decoded_full.update, reconstructed_full).valid
    );
    REQUIRE(same_snapshot_bits(reconstructed_full, full.resulting_snapshot));

    auto const& current =
        simnet::update_synthetic_world_snapshot(snapshot_settings, changes, 101U, synthetic_state);
    auto delta = simnet::encode_snapshot(
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
    CHECK(delta.report.delta.candidate_count == 32U);
    CHECK(delta.report.delta.changed_existing_count == 4U);
    CHECK(delta.report.delta.unchanged_count == 28U);
    CHECK(delta.report.delta.masked_existing_upsert_count == 4U);
    CHECK(delta.report.delta.classification_inclusion_count == 0U);
    CHECK(delta.report.delta.position_inclusion_count == 4U);
    CHECK(delta.report.delta.heading_inclusion_count == 0U);
    CHECK(delta.report.delta.hue_inclusion_count == 0U);
    CHECK(
        delta.report.delta.actual_upsert_representation_bytes <
        delta.report.delta.complete_record_equivalent_bytes
    );

    auto compressor = simnet::ZstdCompressor{};
    auto decompressor = simnet::ZstdDecompressor{};
    auto compressed = std::vector<simnet::Byte>{};
    auto const compression = simnet::compress_bytes(
        compressor,
        delta.update.bytes,
        1,
        {.max_uncompressed_bytes = 4096U, .max_output_bytes = 4096U},
        simnet::CompressionEnvelopePolicy::Always,
        compressed
    );
    REQUIRE(compression.valid);

    auto const packet_settings = simnet::PacketizationSettings{
        .enabled = true,
        .max_payload_bytes = 64U,
        .max_group_bytes = 4096U,
        .max_chunks_per_group = 128U,
        .max_in_flight_groups = 4U,
        .max_incomplete_bytes = 8192U,
        .reassembly_timeout = simnet::Nanoseconds{1'000'000},
    };
    auto prepared = simnet::PreparedByteGroup{};
    auto const preparation = simnet::prepare_byte_group(
        packet_settings,
        delta.update.sequence,
        std::move(compressed),
        prepared
    );
    REQUIRE(preparation.outcome == simnet::GroupPreparationOutcome::Prepared);
    REQUIRE(preparation.chunk_count > 1U);

    auto reassembly = simnet::ReassemblyState{};
    auto serialization_scratch = std::vector<simnet::Byte>{};
    auto completed = simnet::CompletedByteGroup{};
    for (auto index = std::uint16_t{}; index < prepared.chunk_count; ++index)
    {
        auto const packet =
            simnet::serialize_group_chunk(packet_settings, prepared, index, serialization_scratch);
        auto accepted =
            simnet::accept_group_packet(packet_settings, reassembly, packet, simnet::Nanoseconds{});
        if (accepted.kind == simnet::ReassemblyResultKind::Complete)
        {
            completed = std::move(accepted.completed);
        }
        else
        {
            CHECK(accepted.kind == simnet::ReassemblyResultKind::Incomplete);
        }
    }
    REQUIRE(completed.group_id == delta.update.sequence);

    auto decompressed = std::vector<simnet::Byte>{};
    auto const decompression = simnet::decompress_bytes(
        decompressor,
        completed.bytes,
        {.max_uncompressed_bytes = 4096U, .max_output_bytes = 4096U},
        decompressed
    );
    REQUIRE(decompression.valid);
    REQUIRE(decompressed == delta.update.bytes);
    auto const decoded_delta = simnet::decode_update(
        pipeline,
        decode_state,
        {
            .bytes = decompressed,
            .baseline_snapshot = &reconstructed_full,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(decoded_delta.report.valid);
    auto reconstructed = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot(&reconstructed_full, decoded_delta.update, reconstructed)
            .valid
    );
    CHECK(same_snapshot_bits(reconstructed, delta.resulting_snapshot));
}

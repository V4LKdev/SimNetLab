#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <flecs.h>
#include <optional>
#include <utility>
#include <vector>

import simnet.core;
import simnet.game_client;
import simnet.game_server;
import simnet.pipeline;
import simnet.snapshot;
import simnet.transport;

namespace
{
    constexpr auto boid_count = std::uint32_t { 10 };

    [[nodiscard]] simnet::BoidState test_boid(
        simnet::EntityNetId id,
        std::uint32_t index,
        simnet::Tick tick
    )
    {
        auto const base = static_cast<float>(index);
        return {
            .id = id,
            .position = { base * 2.0F + static_cast<float>(tick) * 0.25F, base * 0.5F, 0.0F },
            .heading = { 1.0F, 0.0F, 0.0F },
            .hue = static_cast<std::uint8_t>((index * 23U) & 0xFFU),
        };
    }

    void populate_world(flecs::world& world, simnet::Tick tick)
    {
        for (std::uint32_t index = 0; index < boid_count; ++index) {
            auto const id = static_cast<simnet::EntityNetId>(index + 1U);
            static_cast<void>(simnet::upsert_authoritative_boid(world, test_boid(id, index, tick)));
        }
        if (tick == 2) {
            static_cast<void>(simnet::delete_authoritative_boid(world, boid_count));
        }
    }

    void record_received(simnet::SnapshotAck& ack, simnet::SequenceId sequence)
    {
        auto const previous = ack.newest_received_snapshot;
        if (previous == 0) {
            ack.newest_received_snapshot = sequence;
            return;
        }

        auto const shift = sequence - previous;
        auto const shifted_history = shift >= 32U ? 0U : ack.received_mask << shift;
        ack.received_mask = shifted_history | (1U << (shift - 1U));
        ack.newest_received_snapshot = sequence;
    }

    [[nodiscard]] simnet::WorldSnapshot single_boid_snapshot(simnet::Tick tick)
    {
        auto snapshot = simnet::WorldSnapshot {};
        snapshot.tick = tick;
        snapshot.ids.push_back(1);
        snapshot.positions.push_back({ static_cast<float>(tick), 0.0F, 0.0F });
        snapshot.headings.push_back({ 1.0F, 0.0F, 0.0F });
        snapshot.hues.push_back(0);
        return snapshot;
    }
}

TEST_CASE("five-tick replication contract remains intact", "[replication]")
{
    auto pipeline = simnet::make_snapshot_pipeline();
    pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Quantization;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::OctHeading;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::BitPacking;
    pipeline.send_interval.interval_ticks = 2;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(400.0F);

    auto server_world = flecs::world {};
    auto client_world = flecs::world {};
    simnet::register_server_game(server_world);
    simnet::register_client_game(client_world);

    auto encode_state = simnet::ClientReplicationState {};
    auto decode_state = simnet::ClientReplicationState {};
    auto encode_scratch = simnet::PipelineScratch {};
    auto decode_scratch = simnet::PipelineScratch {};
    auto acknowledged_snapshot = simnet::WorldSnapshot {};
    auto acknowledged_sequence = simnet::SequenceId {};
    auto ack = simnet::SnapshotAck {};
    auto emitted_index = std::size_t {};

    constexpr auto expected_sequences = std::array<simnet::SequenceId, 3> { 1, 2, 3 };
    constexpr auto expected_baselines = std::array<simnet::SequenceId, 3> { 0, 1, 2 };
    constexpr auto expected_kinds = std::array {
        simnet::SnapshotKind::FullReplace,
        simnet::SnapshotKind::Patch,
        simnet::SnapshotKind::Patch,
    };

    for (simnet::Tick tick = 0; tick < 5; ++tick) {
        populate_world(server_world, tick);

        auto snapshot = simnet::WorldSnapshot {};
        auto const extraction = simnet::extract_world_snapshot(server_world, tick, snapshot);
        REQUIRE(extraction.valid);

        auto const encoded = simnet::encode_snapshot(
            pipeline,
            encode_state,
            encode_scratch,
            {
                .snapshot = &snapshot,
                .baseline_snapshot = acknowledged_sequence != 0 ? &acknowledged_snapshot : nullptr,
                .baseline_sequence = acknowledged_sequence,
            }
        );
        if (encoded.kind == simnet::EncodeResultKind::Skipped) {
            continue;
        }

        REQUIRE(emitted_index < expected_sequences.size());
        CHECK(encoded.packet.sequence == expected_sequences[emitted_index]);
        CHECK(encoded.report.baseline_sequence == expected_baselines[emitted_index]);
        CHECK(encoded.report.snapshot_kind == expected_kinds[emitted_index]);

        auto const decoded = simnet::decode_packet(
            pipeline,
            decode_state,
            decode_scratch,
            {
                .bytes = encoded.packet.bytes,
                .applied_baseline_sequence = ack.newest_applied_snapshot,
            }
        );
        REQUIRE(decoded.report.valid);

        auto const applied = simnet::apply_client_snapshot_patch(client_world, decoded.patch);
        REQUIRE(applied.valid);

        record_received(ack, decoded.report.sequence);
        ack.newest_applied_snapshot = decoded.report.sequence;
        acknowledged_snapshot = snapshot;
        acknowledged_sequence = decoded.report.sequence;
        ++emitted_index;
    }

    REQUIRE(emitted_index == 3);
    CHECK(ack.newest_received_snapshot == 3);
    CHECK(ack.received_mask == 3);
    CHECK(ack.newest_applied_snapshot == 3);

    auto const& client_clock = client_world.get<simnet::ClientReplicationClock>();
    auto const& client_index = client_world.get<simnet::ClientReplicationIndex>();
    CHECK(client_clock.latest_tick == 4);
    CHECK(client_index.size() == boid_count);

    auto extracted_client_snapshot = simnet::WorldSnapshot {};
    auto const client_extraction = simnet::extract_client_world_snapshot(
        client_world,
        client_clock.latest_tick,
        extracted_client_snapshot
    );
    REQUIRE(client_extraction.valid);
    CHECK(client_extraction.entity_count == boid_count);
    CHECK(extracted_client_snapshot.tick == 4);
    CHECK(extracted_client_snapshot.ids == client_index.ids);
}

TEST_CASE("authoritative boid mutations use a private indexed lifecycle", "[replication]")
{
    auto world = flecs::world {};
    simnet::register_server_game(world);

    auto boids = std::vector<simnet::BoidState> {};
    boids.push_back(test_boid(1, 0, 0));
    boids.push_back(test_boid(2, 1, 0));
    boids.push_back(test_boid(3, 2, 0));
    auto const appended = simnet::append_authoritative_boids(world, boids);
    REQUIRE(appended.success());
    CHECK(appended.spawned_count == 3U);
    CHECK(simnet::authoritative_boid_count(world) == 3U);

    auto updated = test_boid(2, 9, 4);
    updated.hue = 201U;
    REQUIRE(simnet::upsert_authoritative_boid(world, updated).is_alive());
    CHECK(simnet::authoritative_boid_count(world) == 3U);
    REQUIRE(simnet::delete_authoritative_boid(world, 1));
    CHECK(simnet::authoritative_boid_count(world) == 2U);

    auto snapshot = simnet::WorldSnapshot {};
    auto const extracted = simnet::extract_world_snapshot(world, 4, snapshot);
    REQUIRE(extracted.valid);
    CHECK(snapshot.ids == std::vector<simnet::EntityNetId> { 2, 3 });
    CHECK(snapshot.hues[0] == 201U);
    auto const ids_capacity = snapshot.ids.capacity();

    auto updated_again = test_boid(3, 12, 5);
    updated_again.hue = 99U;
    REQUIRE(simnet::upsert_authoritative_boid(world, updated_again).is_alive());
    auto const extracted_again = simnet::extract_world_snapshot(world, 5, snapshot);
    REQUIRE(extracted_again.valid);
    CHECK(snapshot.tick == 5U);
    CHECK(snapshot.ids == std::vector<simnet::EntityNetId> { 2, 3 });
    CHECK(snapshot.positions[1].x == updated_again.position.x);
    CHECK(snapshot.positions[1].y == updated_again.position.y);
    CHECK(snapshot.positions[1].z == updated_again.position.z);
    CHECK(snapshot.hues[1] == 99U);
    CHECK(snapshot.ids.capacity() >= ids_capacity);

    auto invalid = std::vector<simnet::BoidState> {
        test_boid(4, 3, 4),
        test_boid(4, 4, 4),
    };
    auto const rejected = simnet::append_authoritative_boids(world, invalid);
    CHECK_FALSE(rejected.success());
    CHECK(rejected.error == simnet::AuthoritativeSpawnError::NonAscendingIds);
    CHECK(simnet::authoritative_boid_count(world) == 2U);

    auto overlapping = std::vector<simnet::BoidState> { test_boid(3, 3, 4) };
    auto const overlap_rejected = simnet::append_authoritative_boids(world, overlapping);
    CHECK_FALSE(overlap_rejected.success());
    CHECK(overlap_rejected.error == simnet::AuthoritativeSpawnError::ExistingIdOverlap);
    CHECK(overlap_rejected.failing_index == std::optional<std::size_t> { 0U });
    CHECK(simnet::authoritative_boid_count(world) == 2U);

    auto malformed = std::vector<simnet::BoidState> { test_boid(4, 3, 4) };
    malformed.front().heading = {};
    auto const malformed_rejected = simnet::append_authoritative_boids(world, malformed);
    CHECK_FALSE(malformed_rejected.success());
    CHECK(malformed_rejected.error == simnet::AuthoritativeSpawnError::InvalidBoidState);
    CHECK(malformed_rejected.failing_index == std::optional<std::size_t> { 0U });
    CHECK(simnet::authoritative_boid_count(world) == 2U);
}

TEST_CASE("evicted acknowledged snapshot falls back to FullReplace", "[replication]")
{
    auto pipeline = simnet::make_snapshot_pipeline();
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    auto encode_state = simnet::ClientReplicationState {};
    auto decode_state = simnet::ClientReplicationState {};
    auto encode_scratch = simnet::PipelineScratch {};
    auto decode_scratch = simnet::PipelineScratch {};
    auto const first_snapshot = single_boid_snapshot(1);

    auto const first_packet = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        { .snapshot = &first_snapshot }
    );
    REQUIRE(first_packet.report.snapshot_kind == simnet::SnapshotKind::FullReplace);

    auto retained_snapshots = std::deque<std::pair<simnet::SequenceId, simnet::WorldSnapshot>> {};
    retained_snapshots.push_back({ first_packet.packet.sequence, first_snapshot });
    auto acknowledged_baseline = std::optional { first_packet.packet.sequence };
    retained_snapshots.clear();

    auto const retained = std::find_if(
        retained_snapshots.begin(),
        retained_snapshots.end(),
        [sequence = *acknowledged_baseline](auto const& entry) { return entry.first == sequence; }
    );
    if (retained == retained_snapshots.end()) {
        acknowledged_baseline.reset();
    }

    auto const next_snapshot = single_boid_snapshot(2);
    auto const next_packet = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        {
            .snapshot = &next_snapshot,
            .baseline_snapshot = nullptr,
            .baseline_sequence = 0,
        }
    );
    CHECK_FALSE(acknowledged_baseline.has_value());
    CHECK(next_packet.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    CHECK(next_packet.report.baseline_sequence == 0);

    auto const decoded = simnet::decode_packet(
        pipeline,
        decode_state,
        decode_scratch,
        {
            .bytes = next_packet.packet.bytes,
            .applied_baseline_sequence = first_packet.packet.sequence,
        }
    );
    CHECK(decoded.report.valid);
    CHECK(decoded.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
}

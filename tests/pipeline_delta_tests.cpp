#include <catch2/catch_test_macros.hpp>
#include <vector>

import simnet.core;
import simnet.pipeline;
import simnet.snapshot;

namespace
{
    [[nodiscard]] simnet::WorldSnapshot make_snapshot(
        simnet::Tick tick,
        std::vector<simnet::BoidState> const& boids
    )
    {
        auto snapshot = simnet::WorldSnapshot {};
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
}

TEST_CASE("delta pipeline preserves baseline and patch semantics", "[pipeline][delta]")
{
    auto pipeline = simnet::make_raw_snapshot_pipeline();
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;

    auto const baseline = make_snapshot(0, {
        { .id = 1, .position = { 1.0F, 0.0F, 0.0F }, .heading = { 1.0F, 0.0F, 0.0F }, .hue = 10 },
        { .id = 2, .position = { 2.0F, 0.0F, 0.0F }, .heading = { 1.0F, 0.0F, 0.0F }, .hue = 20 },
        { .id = 3, .position = { 3.0F, 0.0F, 0.0F }, .heading = { 1.0F, 0.0F, 0.0F }, .hue = 30 },
    });
    auto const current = make_snapshot(1, {
        { .id = 1, .position = { 1.0F, 0.0F, 0.0F }, .heading = { 1.0F, 0.0F, 0.0F }, .hue = 10 },
        { .id = 2, .position = { 20.0F, 0.0F, 0.0F }, .heading = { 1.0F, 0.0F, 0.0F }, .hue = 20 },
        { .id = 4, .position = { 4.0F, 0.0F, 0.0F }, .heading = { 1.0F, 0.0F, 0.0F }, .hue = 40 },
    });

    auto encode_state = simnet::ClientReplicationState {};
    auto decode_state = simnet::ClientReplicationState {};
    auto encode_scratch = simnet::PipelineScratch {};
    auto decode_scratch = simnet::PipelineScratch {};

    auto const full = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        { .snapshot = &baseline }
    );
    REQUIRE(full.kind == simnet::EncodeResultKind::Packet);
    REQUIRE(full.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    REQUIRE(full.report.baseline_sequence == 0);

    auto const full_decoded = simnet::decode_packet(
        pipeline,
        decode_state,
        decode_scratch,
        { .bytes = full.packet.bytes }
    );
    REQUIRE(full_decoded.report.valid);

    auto const delta = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        {
            .snapshot = &current,
            .baseline_snapshot = &baseline,
            .baseline_sequence = full.packet.sequence,
        }
    );
    REQUIRE(delta.kind == simnet::EncodeResultKind::Packet);
    REQUIRE(delta.report.delta);
    REQUIRE(delta.report.snapshot_kind == simnet::SnapshotKind::Patch);
    REQUIRE(delta.report.baseline_sequence == full.packet.sequence);
    REQUIRE(delta.report.upsert_count == 2);
    REQUIRE(delta.report.delete_count == 1);

    auto const sequence_before_rejection = decode_state.latest_remote_sequence;
    auto const rejected = simnet::decode_packet(
        pipeline,
        decode_state,
        decode_scratch,
        {
            .bytes = delta.packet.bytes,
            .applied_baseline_sequence = 0,
        }
    );
    REQUIRE_FALSE(rejected.report.valid);
    REQUIRE(rejected.patch.upserts.empty());
    REQUIRE(rejected.patch.deletes.empty());
    REQUIRE(decode_state.latest_remote_sequence == sequence_before_rejection);

    auto const decoded = simnet::decode_packet(
        pipeline,
        decode_state,
        decode_scratch,
        {
            .bytes = delta.packet.bytes,
            .applied_baseline_sequence = full.packet.sequence,
        }
    );
    REQUIRE(decoded.report.valid);
    REQUIRE(decoded.patch.upserts.size() == 2);
    CHECK(decoded.patch.upserts[0].id == 2);
    CHECK(decoded.patch.upserts[1].id == 4);
    CHECK(decoded.patch.deletes == std::vector<simnet::EntityNetId> { 3 });
}

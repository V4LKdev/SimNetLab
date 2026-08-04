#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <type_traits>

import simnet.telemetry;
import simnet.core;
import simnet.snapshot;

static_assert(std::is_trivially_move_constructible_v<simnet::ServerReplicationMeasurement>);
static_assert(std::is_trivially_move_constructible_v<simnet::ClientReplicationMeasurement>);
static_assert(std::is_same_v<
              decltype(simnet::ServerReplicationMeasurement::encode_cpu_time),
              simnet::Nanoseconds>);
static_assert(std::is_same_v<
              decltype(simnet::ClientReplicationMeasurement::sink_application_cpu_time),
              simnet::Nanoseconds>);

TEST_CASE("Server replication measurements preserve byte ownership", "[telemetry][server]")
{
    auto measurements = simnet::ServerReplicationMeasurements{};
    auto const skipped = simnet::ServerReplicationMeasurement{
        .tick = 7,
        .outcome = simnet::ServerReplicationOutcome::Skipped,
        .source_entity_count = 4,
    };
    measurements.observe(skipped);

    CHECK(measurements.attempt_count == 1);
    CHECK(measurements.sent_count == 0);
    CHECK(measurements.latest_attempt->tick == 7);
    CHECK_FALSE(measurements.latest_sent.has_value());

    auto const sent = simnet::ServerReplicationMeasurement{
        .tick = 8,
        .sequence = 3,
        .outcome = simnet::ServerReplicationOutcome::Sent,
        .source_entity_count = 4,
        .selected_entity_count = 3,
        .upsert_count = 3,
        .encoded_update_bytes = 96,
        .application_payload_bytes = 96,
        .transport_payload_bytes = 96,
    };
    measurements.observe(sent);

    REQUIRE(measurements.latest_sent.has_value());
    CHECK(measurements.attempt_count == 2);
    CHECK(measurements.sent_count == 1);
    CHECK(measurements.latest_sent->sequence == 3);
    CHECK(measurements.latest_sent->selected_entity_count == 3);
    CHECK(measurements.latest_sent->encoded_update_bytes == 96);
    CHECK(measurements.latest_sent->application_payload_bytes == 96);
    CHECK(measurements.latest_sent->transport_payload_bytes == 96);
    CHECK(measurements.latest_sent->total_replication_cpu_time == simnet::Nanoseconds{});
}

TEST_CASE("Client applied measurements exclude failed attempts", "[telemetry][client]")
{
    auto measurements = simnet::ClientReplicationMeasurements{};
    auto const failed = simnet::ClientReplicationMeasurement{
        .tick = 10,
        .sequence = 5,
        .outcome = simnet::ClientReplicationOutcome::SinkApplicationFailed,
        .encoded_update_bytes = 128,
        .application_payload_bytes = 128,
        .transport_payload_bytes = 128,
        .upsert_count = 4,
        .reconstructed_entity_count = 4,
        .final_sink_entity_count = 0,
        .sink_application_cpu_time = std::chrono::nanoseconds{2},
    };
    measurements.observe(failed);

    CHECK(measurements.attempt_count == 1);
    CHECK(measurements.applied_count == 0);
    CHECK_FALSE(measurements.latest_applied.has_value());
    CHECK(measurements.latest_attempt->total_receive_to_applied_cpu_time == simnet::Nanoseconds{});

    auto const applied = simnet::ClientReplicationMeasurement{
        .tick = 11,
        .sequence = 6,
        .baseline_sequence = 5,
        .snapshot_kind = simnet::SnapshotKind::Patch,
        .outcome = simnet::ClientReplicationOutcome::Applied,
        .encoded_update_bytes = 160,
        .application_payload_bytes = 160,
        .transport_payload_bytes = 160,
        .upsert_count = 3,
        .delete_count = 1,
        .reconstructed_entity_count = 6,
        .final_sink_entity_count = 6,
        .total_receive_to_applied_cpu_time = std::chrono::nanoseconds{9},
    };
    measurements.observe(applied);

    REQUIRE(measurements.latest_applied.has_value());
    CHECK(measurements.attempt_count == 2);
    CHECK(measurements.applied_count == 1);
    CHECK(measurements.latest_applied->sequence == 6);
    CHECK(measurements.latest_applied->baseline_sequence == 5);
    CHECK(measurements.latest_applied->snapshot_kind == simnet::SnapshotKind::Patch);
    CHECK(measurements.latest_applied->upsert_count == 3);
    CHECK(measurements.latest_applied->delete_count == 1);
    CHECK(measurements.latest_applied->reconstructed_entity_count == 6);
    CHECK(measurements.latest_applied->final_sink_entity_count == 6);
    CHECK(measurements.latest_applied->total_receive_to_applied_cpu_time.count() == 9);
}

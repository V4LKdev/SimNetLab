#include <catch2/catch_test_macros.hpp>

import simnet.app_evidence;
import simnet.pipeline;
import simnet.snapshot;
import simnet.telemetry;

TEST_CASE(
    "Server evidence flattening preserves production encode report primitives",
    "[telemetry][pipeline][evidence]"
)
{
    auto measurement = simnet::ServerReplicationMeasurement{
        .area_of_interest_mode = "radius",
    };
    auto report = simnet::EncodeReport{
        .tick = 40,
        .sequence = 9,
        .baseline_sequence = 8,
        .snapshot_kind = simnet::SnapshotKind::Patch,
        .upsert_count = 7,
        .delete_count = 2,
        .representation =
            {
                .layout = simnet::EntityRecordLayout::Quantized,
                .record_bytes = 18,
                .quality_sample_count = 7,
                .position_error_sum = 0.02,
                .position_error_maximum = 0.004,
                .heading_angular_error_degrees_sum = 0.1,
                .heading_angular_error_degrees_maximum = 0.03,
            },
        .delta =
            {
                .candidate_count = 12,
                .unchanged_count = 5,
                .changed_existing_count = 4,
                .spawned_count = 3,
                .produced_upsert_count = 7,
                .whole_record_existing_upsert_count = 1,
                .masked_existing_upsert_count = 3,
                .classification_inclusion_count = 1,
                .position_inclusion_count = 2,
                .heading_inclusion_count = 3,
                .hue_inclusion_count = 4,
                .complete_record_equivalent_bytes = 126,
                .actual_upsert_representation_bytes = 71,
            },
        .area_of_interest =
            {
                .source_available = true,
                .source_entity_count = 20,
                .candidate_count = 15,
                .retained_count = 12,
                .culled_count = 8,
            },
        .level_of_detail = {
            .mode = simnet::LevelOfDetailMode::DistanceBands,
            .population = {.near = 2, .medium = 4, .far = 6},
            .serviced = {.near = 2, .medium = 2, .far = 1},
            .pending_due_count = 3,
            .transition_count = 1,
            .forced_immediate_count = 2,
            .recovery_forced_count = 1,
            .deletions_bypassing_count = 2,
            .full_replace_override_count = 1,
        },
    };
    auto state = simnet::ClientReplicationState{
        .incremental_cursor = 17,
        .incremental_seeded = true,
        .level_of_detail_schedule = {},
    };

    simnet::app::flatten_server_encode_report(measurement, report, state);

    CHECK(measurement.tick == 40);
    CHECK(measurement.sequence == 9);
    CHECK(measurement.baseline_sequence == 8);
    CHECK(measurement.selected_entity_count == 12);
    CHECK(measurement.area_of_interest_source_status == "available");
    CHECK(measurement.area_of_interest_candidate_count == 15);
    CHECK(measurement.area_of_interest_culled_count == 8);
    CHECK(measurement.lod_near_population == 2);
    CHECK(measurement.lod_medium_scheduled == 2);
    CHECK(measurement.lod_recovery_forced_count == 1);
    CHECK(measurement.delta_unchanged_count == 5);
    CHECK(measurement.delta_masked_existing_count == 3);
    CHECK(measurement.delta_heading_field_count == 3);
    CHECK(measurement.complete_record_equivalent_bytes == 126);
    CHECK(measurement.sparse_record_bytes == 71);
    CHECK(measurement.representation_layout == "quantized");
    CHECK(measurement.complete_record_bytes == 18);
    CHECK(measurement.representation_quality_sample_count == 7);
    CHECK(measurement.position_error_maximum == 0.004);
    CHECK(measurement.heading_error_degrees_maximum == 0.03);
    CHECK(measurement.incremental_cursor_after == 17);
    CHECK(measurement.incremental_seeded_after);

    auto no_area_of_interest = simnet::ServerReplicationMeasurement{};
    simnet::app::flatten_server_encode_report(no_area_of_interest, report, state);
    CHECK(no_area_of_interest.area_of_interest_source_status == "not_required");
}

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
    auto measurement = simnet::ServerReplicationMeasurement{};
    auto report = simnet::EncodeReport{
        .tick = 40,
        .sequence = 9,
        .baseline_sequence = 8,
        .snapshot_kind = simnet::SnapshotKind::Patch,
        .upsert_count = 7,
        .delete_count = 2,
        .recovery_forced_addition_count = 3,
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
            .deletions_bypassing_count = 2,
            .full_replace_override_count = 1,
        },
    };

    simnet::app::flatten_server_encode_report(measurement, report);

    CHECK(measurement.tick == 40);
    CHECK(measurement.sequence == 9);
    CHECK(measurement.baseline_sequence == 8);
    CHECK(measurement.selected_entity_count == 12);
    CHECK(measurement.recovery_forced_upsert_count == 3);
    CHECK(measurement.aoi_candidate_entity_count == 15);
    CHECK(measurement.lod_near_population == 2);
    CHECK(measurement.lod_medium_scheduled == 2);
    CHECK(measurement.delta_unchanged_entity_count == 5);
    CHECK(measurement.delta_field_mask_entity_count == 3);
    CHECK(measurement.delta_heading_field_count == 3);
    CHECK(measurement.delta_complete_record_equivalent_bytes == 126);
    CHECK(measurement.delta_encoded_record_bytes == 71);
    CHECK(measurement.representation_sample_count == 7);
    CHECK(measurement.position_error_world_units_max == 0.004);
    CHECK(measurement.heading_error_degrees_max == 0.03);
}

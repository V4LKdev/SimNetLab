module simnet.app_evidence;

namespace simnet::app
{
    void flatten_server_encode_report(
        ServerReplicationMeasurement& measurement,
        EncodeReport const& report
    ) noexcept
    {
        measurement.tick = report.tick;
        measurement.sequence = report.sequence;
        measurement.baseline_sequence = report.baseline_sequence;
        measurement.snapshot_kind = report.snapshot_kind;
        measurement.selected_entity_count = report.area_of_interest.retained_count;
        measurement.upsert_count = report.upsert_count;
        measurement.delete_count = report.delete_count;
        measurement.recovery_forced_upsert_count = report.recovery_forced_addition_count;
        measurement.aoi_candidate_entity_count = report.area_of_interest.candidate_count;

        auto const& lod = report.level_of_detail;
        measurement.lod_near_population = lod.population.near;
        measurement.lod_medium_population = lod.population.medium;
        measurement.lod_far_population = lod.population.far;
        measurement.lod_near_scheduled = lod.serviced.near;
        measurement.lod_medium_scheduled = lod.serviced.medium;
        measurement.lod_far_scheduled = lod.serviced.far;
        measurement.lod_pending_due_count = lod.pending_due_count;
        measurement.lod_transition_count = lod.transition_count;
        measurement.lod_forced_immediate_count = lod.forced_immediate_count;

        auto const& delta = report.delta;
        measurement.delta_unchanged_entity_count = delta.unchanged_count;
        measurement.delta_spawned_entity_count = delta.spawned_count;
        measurement.delta_whole_record_entity_count = delta.whole_record_existing_upsert_count;
        measurement.delta_field_mask_entity_count = delta.masked_existing_upsert_count;
        measurement.delta_classification_field_count = delta.classification_inclusion_count;
        measurement.delta_position_field_count = delta.position_inclusion_count;
        measurement.delta_heading_field_count = delta.heading_inclusion_count;
        measurement.delta_hue_field_count = delta.hue_inclusion_count;
        measurement.delta_complete_record_equivalent_bytes =
            delta.complete_record_equivalent_bytes;
        measurement.delta_encoded_record_bytes = delta.actual_upsert_representation_bytes;

        auto const& representation = report.representation;
        measurement.representation_sample_count = representation.quality_sample_count;
        measurement.position_error_world_units_sum = representation.position_error_sum;
        measurement.position_error_world_units_max = representation.position_error_maximum;
        measurement.heading_error_degrees_sum = representation.heading_angular_error_degrees_sum;
        measurement.heading_error_degrees_max =
            representation.heading_angular_error_degrees_maximum;
    }
}

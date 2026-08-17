module;

#include <string_view>

module simnet.app_evidence;

namespace
{
    using namespace simnet;
    using namespace simnet::app;

    [[nodiscard]] constexpr std::string_view
    entity_record_layout_name(EntityRecordLayout layout) noexcept
    {
        switch (layout)
        {
            case EntityRecordLayout::Raw:
                return "raw";
            case EntityRecordLayout::Quantized:
                return "quantized";
            case EntityRecordLayout::QuantizedOctHeading:
                return "quantized_oct_heading";
            case EntityRecordLayout::BitPackedQuantizedOctHeading:
                return "bit_packed_quantized_oct_heading";
        }
        return "unknown";
    }

}

namespace simnet::app
{
    void flatten_server_encode_report(
        ServerReplicationMeasurement& measurement,
        EncodeReport const& report,
        ClientReplicationState const& state
    ) noexcept
    {
        measurement.tick = report.tick;
        measurement.sequence = report.sequence;
        measurement.baseline_sequence = report.baseline_sequence;
        measurement.snapshot_kind = report.snapshot_kind;
        measurement.incremental_cursor_after = state.incremental_cursor;
        measurement.incremental_seeded_after = state.incremental_seeded;
        measurement.selected_entity_count = report.area_of_interest.retained_count;
        measurement.upsert_count = report.upsert_count;
        measurement.delete_count = report.delete_count;
        if (measurement.area_of_interest_mode != "none")
        {
            measurement.area_of_interest_source_status =
                report.area_of_interest.source_available ? "available" : "unavailable";
        }
        measurement.area_of_interest_candidate_count = report.area_of_interest.candidate_count;
        measurement.area_of_interest_culled_count = report.area_of_interest.culled_count;

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
        measurement.lod_recovery_forced_count = lod.recovery_forced_count;
        measurement.lod_deletions_bypassing_count = lod.deletions_bypassing_count;
        measurement.lod_full_replace_override_count = lod.full_replace_override_count;

        auto const& delta = report.delta;
        measurement.delta_candidate_count = delta.candidate_count;
        measurement.delta_unchanged_count = delta.unchanged_count;
        measurement.delta_changed_existing_count = delta.changed_existing_count;
        measurement.delta_spawned_count = delta.spawned_count;
        measurement.delta_whole_record_existing_count = delta.whole_record_existing_upsert_count;
        measurement.delta_masked_existing_count = delta.masked_existing_upsert_count;
        measurement.delta_classification_field_count = delta.classification_inclusion_count;
        measurement.delta_position_field_count = delta.position_inclusion_count;
        measurement.delta_heading_field_count = delta.heading_inclusion_count;
        measurement.delta_hue_field_count = delta.hue_inclusion_count;
        measurement.complete_record_equivalent_bytes = delta.complete_record_equivalent_bytes;
        measurement.sparse_record_bytes = delta.actual_upsert_representation_bytes;

        auto const& representation = report.representation;
        measurement.representation_layout = entity_record_layout_name(representation.layout);
        measurement.complete_record_bytes = representation.record_bytes;
        measurement.representation_quality_sample_count = representation.quality_sample_count;
        measurement.position_error_sum = representation.position_error_sum;
        measurement.position_error_maximum = representation.position_error_maximum;
        measurement.heading_error_degrees_sum = representation.heading_angular_error_degrees_sum;
        measurement.heading_error_degrees_maximum =
            representation.heading_angular_error_degrees_maximum;
    }
}

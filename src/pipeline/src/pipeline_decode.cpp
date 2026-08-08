module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module simnet.pipeline;

import :api;
import :types;
import :messages;
import :wire;
import :records;
import :signature;
import :validate;
import simnet.core;
import simnet.snapshot;

namespace
{
    struct InspectedHeader
    {
        simnet::EncodedUpdateHeaderInspection public_result{};
        simnet::pipeline_wire::EncodedUpdateHeader header{};
    };

    [[nodiscard]] bool
    supported_decode_pipeline(simnet::PipelineDefinition const& pipeline) noexcept
    {
        using simnet::PipelineTechniqueFlags;
        auto constexpr supported = static_cast<std::uint32_t>(
            PipelineTechniqueFlags::SendInterval | PipelineTechniqueFlags::Incremental
            | PipelineTechniqueFlags::Quantization | PipelineTechniqueFlags::OctHeading
            | PipelineTechniqueFlags::Delta | PipelineTechniqueFlags::DeltaFieldMask
            | PipelineTechniqueFlags::BitPacking
        );
        auto const requested = static_cast<std::uint32_t>(pipeline.techniques);
        if ((requested & ~supported) != 0U) {
            return false;
        }
        auto const quantized
            = simnet::has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Quantization);
        auto const oct_heading
            = simnet::has_all_flags(pipeline.techniques, PipelineTechniqueFlags::OctHeading);
        auto const bit_packing
            = simnet::has_all_flags(pipeline.techniques, PipelineTechniqueFlags::BitPacking);
        auto const delta
            = simnet::has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Delta);
        auto const field_mask
            = simnet::has_all_flags(pipeline.techniques, PipelineTechniqueFlags::DeltaFieldMask);
        if ((oct_heading && !quantized) || (bit_packing && (!quantized || !oct_heading))
            || (field_mask && !delta)) {
            return false;
        }
        if (!quantized) {
            return true;
        }
        auto const bounds = pipeline.quantization.position_bounds;
        return simnet::is_finite(bounds.min) && simnet::is_finite(bounds.max)
            && bounds.min.x < bounds.max.x && bounds.min.y < bounds.max.y
            && bounds.min.z < bounds.max.z;
    }

    [[nodiscard]] bool checked_payload_size(
        simnet::pipeline_wire::EncodedUpdateHeader const& header,
        std::size_t byte_count
    ) noexcept
    {
        return byte_count >= simnet::pipeline_wire::header_bytes
            && header.payload_bytes == byte_count - simnet::pipeline_wire::header_bytes;
    }

    [[nodiscard]] InspectedHeader inspect_header(
        simnet::PipelineDefinition const& pipeline,
        simnet::ClientReplicationState const& client_state,
        simnet::ByteSpan bytes
    ) noexcept
    {
        using simnet::EncodedUpdateHeaderError;
        auto result = InspectedHeader{};
        auto fail = [&](EncodedUpdateHeaderError error) {
            result.public_result.error = error;
            return result;
        };

        if (!supported_decode_pipeline(pipeline)) {
            return fail(EncodedUpdateHeaderError::UnsupportedPipeline);
        }
        if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            return fail(EncodedUpdateHeaderError::ByteCountOutOfRange);
        }
        if (bytes.size() < simnet::pipeline_wire::header_bytes
            || !simnet::pipeline_wire::read_header(bytes, result.header)) {
            return fail(EncodedUpdateHeaderError::Truncated);
        }

        auto const& header = result.header;
        result.public_result.tick = header.tick;
        result.public_result.sequence = header.sequence;
        result.public_result.baseline_sequence = header.baseline_sequence;
        result.public_result.snapshot_kind = header.snapshot_kind;

        if (header.magic != simnet::pipeline_wire::encoded_update_magic) {
            return fail(EncodedUpdateHeaderError::InvalidMagic);
        }
        if (header.protocol != simnet::pipeline_wire::protocol_version
            || header.schema != simnet::pipeline_wire::encoded_update_schema(pipeline)) {
            return fail(EncodedUpdateHeaderError::UnsupportedVersion);
        }
        if (header.decode_signature
            != simnet::pipeline_signature::make_pipeline_decode_signature(pipeline)) {
            return fail(EncodedUpdateHeaderError::SignatureMismatch);
        }
        if (header.snapshot_kind != simnet::SnapshotKind::FullReplace
            && header.snapshot_kind != simnet::SnapshotKind::Patch) {
            return fail(EncodedUpdateHeaderError::UnsupportedSnapshotKind);
        }
        if (header.sequence == 0U) {
            return fail(EncodedUpdateHeaderError::ReservedSequence);
        }
        if (header.sequence <= client_state.latest_remote_sequence) {
            return fail(EncodedUpdateHeaderError::StaleSequence);
        }
        if (!checked_payload_size(header, bytes.size())) {
            return fail(EncodedUpdateHeaderError::InvalidPayloadSize);
        }

        if (header.snapshot_kind == simnet::SnapshotKind::FullReplace) {
            if (header.baseline_sequence != 0U) {
                return fail(EncodedUpdateHeaderError::FullReplaceHasBaseline);
            }
        } else {
            auto const patch_supported
                = simnet::has_all_flags(pipeline.techniques, simnet::PipelineTechniqueFlags::Delta)
                || simnet::has_all_flags(
                      pipeline.techniques,
                      simnet::PipelineTechniqueFlags::Incremental
                )
                || pipeline.level_of_detail.mode == simnet::LevelOfDetailMode::DistanceBands;
            if (!patch_supported) {
                return fail(EncodedUpdateHeaderError::PatchUnsupported);
            }
            if (header.baseline_sequence == 0U) {
                return fail(EncodedUpdateHeaderError::PatchHasReservedBaseline);
            }
            if (header.baseline_sequence >= header.sequence) {
                return fail(EncodedUpdateHeaderError::PatchBaselineNotEarlier);
            }
        }

        auto const layout = simnet::pipeline_records::resolve_record_layout(pipeline);
        auto const delete_bytes = static_cast<std::uint64_t>(header.delete_count)
            * simnet::pipeline_wire::delete_record_bytes;
        auto const masked_patch = simnet::pipeline_wire::field_mask_enabled(pipeline)
            && header.snapshot_kind == simnet::SnapshotKind::Patch;
        if (masked_patch) {
            auto const minimum = delete_bytes
                + static_cast<std::uint64_t>(header.upsert_count)
                    * simnet::pipeline_wire::masked_upsert_minimum_bytes;
            auto const maximum = delete_bytes
                + static_cast<std::uint64_t>(header.upsert_count) * (layout.record_bytes + 1U);
            if (header.payload_bytes < minimum || header.payload_bytes > maximum) {
                return fail(EncodedUpdateHeaderError::InvalidPayloadBounds);
            }
        } else {
            auto const expected = delete_bytes
                + static_cast<std::uint64_t>(header.upsert_count) * layout.record_bytes;
            if (header.payload_bytes != expected) {
                return fail(EncodedUpdateHeaderError::InvalidPayloadBounds);
            }
        }
        return result;
    }

    [[nodiscard]] std::string_view
    inspection_error_message(simnet::EncodedUpdateHeaderError error) noexcept
    {
        using enum simnet::EncodedUpdateHeaderError;
        switch (error) {
            case None:
                return {};
            case UnsupportedPipeline:
                return "pipeline does not support requested decode techniques";
            case ByteCountOutOfRange:
                return "encoded update byte count exceeds uint32 range";
            case Truncated:
                return "encoded update is shorter than header";
            case InvalidMagic:
                return "invalid encoded update magic";
            case UnsupportedVersion:
                return "unsupported encoded update version";
            case SignatureMismatch:
                return "encoded update signature does not match local pipeline";
            case UnsupportedSnapshotKind:
                return "unsupported snapshot kind";
            case ReservedSequence:
                return "encoded update sequence 0 is reserved";
            case StaleSequence:
                return "stale or out-of-order encoded update sequence";
            case InvalidPayloadSize:
                return "encoded update payload size does not match header";
            case FullReplaceHasBaseline:
                return "full snapshot baseline sequence must be 0";
            case PatchUnsupported:
                return "patch requires Incremental, Delta, or level of detail";
            case PatchHasReservedBaseline:
                return "patch baseline sequence 0 is reserved";
            case PatchBaselineNotEarlier:
                return "patch baseline must precede update sequence";
            case InvalidPayloadBounds:
                return "encoded update payload counts do not match payload size";
        }
        return "unknown encoded update header error";
    }

    [[nodiscard]] simnet::DecodeOutput
    invalid_decode(std::string error, simnet::EncodedUpdateHeaderInspection const& header = {})
    {
        auto output = simnet::DecodeOutput{};
        output.report.tick = header.tick;
        output.report.sequence = header.sequence;
        output.report.baseline_sequence = header.baseline_sequence;
        output.report.snapshot_kind = header.snapshot_kind;
        output.report.error = std::move(error);
        return output;
    }

    [[nodiscard]] simnet::EntityState
    baseline_entity(simnet::WorldSnapshot const& baseline, std::size_t index) noexcept
    {
        return {
            .id = baseline.ids[index],
            .classification = baseline.classifications[index],
            .position = baseline.positions[index],
            .heading = baseline.headings[index],
            .hue = baseline.hues[index],
        };
    }

    [[nodiscard]] simnet::DecodeOutput decode_update_impl(
        simnet::PipelineDefinition const& pipeline,
        simnet::ClientReplicationState& client_state,
        simnet::DecodeInput const& input,
        bool validate_baseline
    )
    {
        auto const inspected = inspect_header(pipeline, client_state, input.bytes);
        if (!inspected.public_result.valid()) {
            return invalid_decode(
                std::string{inspection_error_message(inspected.public_result.error)},
                inspected.public_result
            );
        }
        auto const& header = inspected.header;
        auto invalid_update = [&](std::string error) {
            return invalid_decode(std::move(error), inspected.public_result);
        };

        if (input.baseline_snapshot == nullptr) {
            if (input.baseline_sequence != 0U) {
                return invalid_update("decode baseline sequence requires a baseline snapshot");
            }
        } else {
            if (header.snapshot_kind != simnet::SnapshotKind::Patch) {
                return invalid_update("decode baseline is valid only for a Patch");
            }
            if (input.baseline_sequence != header.baseline_sequence) {
                return invalid_update("decode baseline sequence does not match encoded update");
            }
            if (validate_baseline) {
                auto const validation = simnet::validate_world_snapshot(*input.baseline_snapshot);
                if (!validation.valid) {
                    return invalid_update("decode baseline is invalid: " + validation.message);
                }
            }
        }

        auto const masked_patch = simnet::pipeline_wire::field_mask_enabled(pipeline)
            && header.snapshot_kind == simnet::SnapshotKind::Patch;
        if (masked_patch && input.baseline_snapshot == nullptr) {
            return invalid_update("field-mask Patch requires its exact retained baseline");
        }

        auto const layout = simnet::pipeline_records::resolve_record_layout(pipeline);
        auto offset = static_cast<std::size_t>(simnet::pipeline_wire::header_bytes);
        auto patch = simnet::SnapshotUpdate{};
        patch.tick = header.tick;
        patch.kind = header.snapshot_kind;
        patch.reserve(header.upsert_count, header.delete_count);

        for (auto index = std::uint32_t{}; index < header.delete_count; ++index) {
            auto id = simnet::EntityNetId{};
            if (!simnet::pipeline_wire::read_u32(input.bytes, offset, id)) {
                return invalid_update("truncated delete id data");
            }
            patch.deletes.push_back(id);
        }

        auto baseline_index = std::size_t{};
        auto previous_upsert_id = simnet::EntityNetId{};
        for (auto index = std::uint32_t{}; index < header.upsert_count; ++index) {
            if (!masked_patch) {
                auto entity = simnet::EntityState{};
                if (!simnet::pipeline_records::read_record(input.bytes, offset, layout, entity)) {
                    return invalid_update("truncated upsert record data");
                }
                patch.upserts.push_back(entity);
                continue;
            }

            auto id = simnet::EntityNetId{};
            auto selector = std::uint8_t{};
            if (!simnet::pipeline_wire::read_u32(input.bytes, offset, id)
                || !simnet::pipeline_wire::read_u8(input.bytes, offset, selector)) {
                return invalid_update("truncated field-mask upsert prefix");
            }
            if (id == 0U || (index != 0U && id <= previous_upsert_id)) {
                return invalid_update("field-mask upsert IDs must be nonzero and ascending");
            }
            previous_upsert_id = id;
            auto const spawn = selector == simnet::pipeline_wire::spawn_record_selector;
            auto const valid_existing_mask
                = selector != 0U && (selector & ~simnet::pipeline_wire::existing_field_mask) == 0U;
            if (!spawn && !valid_existing_mask) {
                return invalid_update("field-mask upsert selector is invalid");
            }

            auto const& baseline = *input.baseline_snapshot;
            auto const found = std::ranges::lower_bound(
                baseline.ids.begin() + static_cast<std::ptrdiff_t>(baseline_index),
                baseline.ids.end(),
                id
            );
            baseline_index = static_cast<std::size_t>(std::distance(baseline.ids.begin(), found));
            auto const exists
                = baseline_index < baseline.size() && baseline.ids[baseline_index] == id;
            if (spawn && exists) {
                return invalid_update("field-mask spawn already exists in the baseline");
            }
            if (!spawn && !exists) {
                return invalid_update("field-mask existing upsert is absent from the baseline");
            }

            auto entity
                = spawn ? simnet::EntityState{.id = id} : baseline_entity(baseline, baseline_index);
            auto const fields = spawn ? simnet::pipeline_wire::existing_field_mask : selector;
            if (!simnet::pipeline_records::read_selected_fields(
                    input.bytes,
                    offset,
                    layout,
                    fields,
                    entity
                )) {
                return invalid_update("truncated field-mask upsert fields");
            }
            patch.upserts.push_back(entity);
        }

        if (offset != input.bytes.size()) {
            return invalid_update("encoded update has trailing bytes");
        }
        auto const validation = simnet::validate_client_snapshot_patch(patch);
        if (!validation.valid) {
            return invalid_update("decoded update is invalid: " + validation.message);
        }

        client_state.latest_remote_sequence = header.sequence;
        return {
            .update = std::move(patch),
            .report = {
                .tick = header.tick,
                .sequence = header.sequence,
                .baseline_sequence = header.baseline_sequence,
                .snapshot_kind = header.snapshot_kind,
                .valid = true,
            },
        };
    }
}

namespace simnet
{
    EncodedUpdateHeaderInspection inspect_encoded_update_header(
        PipelineDefinition const& pipeline,
        ClientReplicationState const& client_state,
        ByteSpan bytes
    ) noexcept
    {
        return inspect_header(pipeline, client_state, bytes).public_result;
    }

    DecodeOutput decode_update(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        DecodeInput const& input
    )
    {
        pipeline_validate::require_supported_pipeline_definition(pipeline);
        pipeline_validate::require_quantization_settings(pipeline);
        return decode_update_impl(pipeline, client_state, input, true);
    }

    DecodeOutput decode_update_unchecked(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        DecodeInput const& input
    )
    {
        pipeline_validate::require_supported_pipeline_definition(pipeline);
        pipeline_validate::require_quantization_settings(pipeline);
        return decode_update_impl(pipeline, client_state, input, false);
    }
}

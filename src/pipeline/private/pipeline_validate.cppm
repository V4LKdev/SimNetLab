module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

/// @brief Private pipeline definition and input validation helpers.
module simnet.pipeline:validate;

import :types;
import simnet.core;
import simnet.snapshot;

namespace simnet::pipeline_validate
{
    /// Ensures the pipeline definition is a valid RawSnapshot profile.
    void require_raw_snapshot(PipelineDefinition const& pipeline)
    {
        if (pipeline.profile != PipelineProfileKind::RawSnapshot) {
            throw std::runtime_error("unsupported pipeline profile");
        }
        if (pipeline.codec != CodecKind::ByteAligned) {
            throw std::runtime_error("unsupported raw snapshot codec");
        }

        auto constexpr supported_techniques = static_cast<std::uint32_t>(
            PipelineTechniqueFlags::SendInterval
                | PipelineTechniqueFlags::Incremental
                | PipelineTechniqueFlags::Quantization
                | PipelineTechniqueFlags::OctHeading
                | PipelineTechniqueFlags::Delta
                | PipelineTechniqueFlags::BitPacking
        );
        auto const requested = static_cast<std::uint32_t>(pipeline.techniques);
        auto const unsupported = requested & ~supported_techniques;
        if (unsupported != 0U) {
            throw std::runtime_error("raw snapshot does not support requested techniques");
        }

        if (has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Incremental)
            && has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Quantization)) {
            throw std::runtime_error("raw snapshot quantization does not support incremental packets yet");
        }
        if (has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Incremental)
            && has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Delta)) {
            throw std::runtime_error("raw snapshot does not support combining incremental and delta selection");
        }
        if (has_all_flags(pipeline.techniques, PipelineTechniqueFlags::OctHeading)
            && !has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Quantization)) {
            throw std::runtime_error("oct heading requires quantization");
        }
        if (has_all_flags(pipeline.techniques, PipelineTechniqueFlags::BitPacking)
            && (!has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Quantization)
                || !has_all_flags(pipeline.techniques, PipelineTechniqueFlags::OctHeading))) {
            throw std::runtime_error("bit packing requires quantization and oct heading");
        }
    }

    /// Validates a WorldSnapshot pointer and its internal contract.
    void require_snapshot(WorldSnapshot const* snapshot, char const* what)
    {
        if (snapshot == nullptr) {
            throw std::runtime_error(std::string { what } + " is null");
        }

        auto const validation = validate_world_snapshot(*snapshot);
        if (!validation.valid) {
            throw std::runtime_error(std::string { what } + " is invalid: " + validation.message);
        }
    }

    /// Rejects counts that cannot be represented by the wire format.
    void require_u32_count(std::size_t count, char const* what)
    {
        if (count > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error(std::string { what } + " exceeds uint32 range");
        }
    }

    /// Validates send interval settings.
    void require_send_interval_settings(PipelineDefinition const& pipeline)
    {
        if (has_all_flags(pipeline.techniques, PipelineTechniqueFlags::SendInterval)
            && pipeline.send_interval.interval_ticks == 0U) {
            throw std::runtime_error("send interval tick count must be greater than 0");
        }
    }

    /// Validates incremental settings.
    void require_incremental_settings(PipelineDefinition const& pipeline)
    {
        if (has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Incremental)
            && pipeline.incremental.max_entities_per_packet == 0U) {
            throw std::runtime_error("incremental max entities per packet must be greater than 0");
        }
    }

    /// Validates quantization settings.
    void require_quantization_settings(PipelineDefinition const& pipeline)
    {
        if (!has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Quantization)) {
            return;
        }

        auto const bounds = pipeline.quantization.position_bounds;
        if (!is_finite(bounds.min) || !is_finite(bounds.max)) {
            throw std::runtime_error("quantization bounds must be finite");
        }
        if (bounds.min.x >= bounds.max.x || bounds.min.y >= bounds.max.y || bounds.min.z >= bounds.max.z) {
            throw std::runtime_error("quantization bounds must have positive extent");
        }
    }

    /// Returns true when the pipeline uses incremental selection.
    [[nodiscard]] bool is_incremental(PipelineDefinition const& pipeline) noexcept
    {
        return has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Incremental);
    }

    /// Returns true when the pipeline uses delta selection.
    [[nodiscard]] bool is_delta(PipelineDefinition const& pipeline) noexcept
    {
        return has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Delta);
    }
}

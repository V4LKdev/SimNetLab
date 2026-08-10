module;

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

/// @brief Private pipeline definition and input validation helpers.
module simnet.pipeline:validate;

import :types;
import simnet.core;
import simnet.snapshot;

namespace simnet::pipeline_validate
{
    /// Ensures the pipeline definition uses a supported technique combination.
    void require_supported_pipeline_definition(PipelineDefinition const& pipeline)
    {
        auto constexpr supported_techniques = static_cast<std::uint32_t>(
            PipelineTechniqueFlags::SendInterval | PipelineTechniqueFlags::Incremental |
            PipelineTechniqueFlags::Quantization | PipelineTechniqueFlags::OctHeading |
            PipelineTechniqueFlags::Delta | PipelineTechniqueFlags::DeltaFieldMask |
            PipelineTechniqueFlags::BitPacking
        );
        auto const requested = static_cast<std::uint32_t>(pipeline.techniques);
        auto const unsupported = requested & ~supported_techniques;
        if (unsupported != 0U)
        {
            throw std::runtime_error("pipeline does not support requested techniques");
        }

        if (has_all_flags(pipeline.techniques, PipelineTechniqueFlags::OctHeading) &&
            !has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Quantization))
        {
            throw std::runtime_error("oct heading requires quantization");
        }
        if (has_all_flags(pipeline.techniques, PipelineTechniqueFlags::BitPacking) &&
            (!has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Quantization) ||
             !has_all_flags(pipeline.techniques, PipelineTechniqueFlags::OctHeading)))
        {
            throw std::runtime_error("bit packing requires quantization and oct heading");
        }
        if (has_all_flags(pipeline.techniques, PipelineTechniqueFlags::DeltaFieldMask) &&
            !has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Delta))
        {
            throw std::runtime_error("delta field mask requires Delta");
        }
    }

    /// Rejects a null WorldSnapshot pointer.
    void require_snapshot_pointer(WorldSnapshot const* snapshot, char const* what)
    {
        if (snapshot == nullptr)
        {
            throw std::runtime_error(std::string{what} + " is null");
        }
    }

    /// Validates a WorldSnapshot pointer and its internal contract.
    void require_snapshot(WorldSnapshot const* snapshot, char const* what)
    {
        require_snapshot_pointer(snapshot, what);

        auto const validation = validate_world_snapshot(*snapshot);
        if (!validation.valid)
        {
            throw std::runtime_error(std::string{what} + " is invalid: " + validation.message);
        }
    }

    /// Rejects counts that cannot be represented by the wire format.
    void require_u32_count(std::size_t count, char const* what)
    {
        if (count > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error(std::string{what} + " exceeds uint32 range");
        }
    }

    /// Validates send interval settings.
    void require_send_interval_settings(PipelineDefinition const& pipeline)
    {
        if (has_all_flags(pipeline.techniques, PipelineTechniqueFlags::SendInterval) &&
            pipeline.send_interval.interval_ticks == 0U)
        {
            throw std::runtime_error("send interval tick count must be greater than 0");
        }
    }

    /// Validates incremental settings.
    void require_incremental_settings(PipelineDefinition const& pipeline)
    {
        if (has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Incremental) &&
            pipeline.incremental.max_entities_per_update == 0U)
        {
            throw std::runtime_error("incremental max entities per update must be greater than 0");
        }
    }

    /// Validates quantization settings.
    void require_quantization_settings(PipelineDefinition const& pipeline)
    {
        if (!has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Quantization))
        {
            return;
        }

        auto const bounds = pipeline.quantization.position_bounds;
        if (!is_finite(bounds.min) || !is_finite(bounds.max))
        {
            throw std::runtime_error("quantization bounds must be finite");
        }
        if (bounds.min.x >= bounds.max.x || bounds.min.y >= bounds.max.y ||
            bounds.min.z >= bounds.max.z)
        {
            throw std::runtime_error("quantization bounds must have positive extent");
        }
    }

    /// Validates the active AOI mode and mode-specific settings.
    void require_area_of_interest_settings(PipelineDefinition const& pipeline)
    {
        auto const& settings = pipeline.area_of_interest;
        switch (settings.mode)
        {
            case AreaOfInterestMode::None:
                if (settings.radius != 0.0F || settings.fov_degrees != 0.0F)
                {
                    throw std::runtime_error("none AOI does not accept radius or FOV settings");
                }
                return;
            case AreaOfInterestMode::Radius:
                if (!std::isfinite(settings.radius) || settings.radius <= 0.0F)
                {
                    throw std::runtime_error("radius AOI requires a positive finite radius");
                }
                if (settings.fov_degrees != 0.0F)
                {
                    throw std::runtime_error("radius AOI does not accept an FOV setting");
                }
                return;
            case AreaOfInterestMode::Fov:
                if (!std::isfinite(settings.radius) || settings.radius <= 0.0F)
                {
                    throw std::runtime_error("FOV AOI requires a positive finite radius");
                }
                if (!std::isfinite(settings.fov_degrees) || settings.fov_degrees <= 0.0F ||
                    settings.fov_degrees > 180.0F)
                {
                    throw std::runtime_error("FOV AOI angle must be in (0, 180]");
                }
                return;
        }
        throw std::runtime_error("unsupported AOI mode");
    }

    /// Validates the active temporal distance-LOD treatment.
    void require_level_of_detail_settings(PipelineDefinition const& pipeline)
    {
        auto const& settings = pipeline.level_of_detail;
        if (settings.mode == LevelOfDetailMode::None)
        {
            if (settings.near_distance != 0.0F || settings.medium_distance != 0.0F ||
                settings.medium_interval_ticks != 0U || settings.far_interval_ticks != 0U)
            {
                throw std::runtime_error("none level of detail accepts no band settings");
            }
            return;
        }
        if (settings.mode != LevelOfDetailMode::DistanceBands)
        {
            throw std::runtime_error("unsupported level-of-detail mode");
        }
        if (pipeline.area_of_interest.mode == AreaOfInterestMode::None)
        {
            throw std::runtime_error("distance LOD requires radius or FOV AOI");
        }
        if (!std::isfinite(settings.near_distance) || settings.near_distance <= 0.0F ||
            !std::isfinite(settings.medium_distance) || settings.medium_distance <= 0.0F)
        {
            throw std::runtime_error("distance LOD requires positive finite distances");
        }
        if (settings.near_distance >= settings.medium_distance)
        {
            throw std::runtime_error("distance LOD requires near distance below medium distance");
        }
        if (settings.medium_distance >= pipeline.area_of_interest.radius)
        {
            throw std::runtime_error("distance LOD medium distance must be below the AOI radius");
        }
        if (settings.medium_interval_ticks < 2U)
        {
            throw std::runtime_error("distance LOD medium interval must be at least 2 ticks");
        }
        if (settings.far_interval_ticks <= settings.medium_interval_ticks)
        {
            throw std::runtime_error("distance LOD far interval must exceed the medium interval");
        }
        if (settings.far_interval_ticks > 65'535U)
        {
            throw std::runtime_error("distance LOD intervals must not exceed 65535 ticks");
        }
    }

    /// Validates one authoritative interest pose.
    void require_interest_source(InterestSource const& source)
    {
        if (!is_finite(source.position) || !is_finite(source.forward))
        {
            throw std::runtime_error("AOI interest source must be finite");
        }
        auto const forward_length_squared = length_squared(source.forward);
        auto const min_length = 1.0F - heading_normalization_tolerance;
        auto const max_length = 1.0F + heading_normalization_tolerance;
        if (forward_length_squared < min_length * min_length ||
            forward_length_squared > max_length * max_length)
        {
            throw std::runtime_error("AOI interest source forward direction must be normalized");
        }
    }

    /// Validates sorted source indices supplied by an application-owned coarse query.
    void require_candidate_indices(std::span<std::uint32_t const> indices, std::size_t source_count)
    {
        auto previous = std::uint32_t{};
        auto first = true;
        for (auto const index : indices)
        {
            if (index >= source_count)
            {
                throw std::runtime_error("AOI candidate index is outside the source snapshot");
            }
            if (!first && index <= previous)
            {
                throw std::runtime_error("AOI candidate indices must be strictly ascending");
            }
            previous = index;
            first = false;
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

    /// Returns true when temporal distance LOD is active.
    [[nodiscard]] bool is_level_of_detail(PipelineDefinition const& pipeline) noexcept
    {
        return pipeline.level_of_detail.mode == LevelOfDetailMode::DistanceBands;
    }
}

module;

#include <bit>
#include <cstdint>

/// @brief Private pipeline decode compatibility signature helpers.
module simnet.pipeline:signature;

import :types;
import :wire;
import simnet.core;

namespace simnet::pipeline_signature
{
    struct DecodeSignatureBuilder
    {
        std::uint64_t value { 14695981039346656037ULL };
    };

    void update_signature_byte(DecodeSignatureBuilder& signature, std::uint8_t value) noexcept
    {
        signature.value ^= value;
        signature.value *= 1099511628211ULL;
    }

    void update_signature_u8(DecodeSignatureBuilder& signature, std::uint8_t value) noexcept
    {
        update_signature_byte(signature, value);
    }

    void update_signature_u16(DecodeSignatureBuilder& signature, std::uint16_t value) noexcept
    {
        update_signature_byte(signature, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        update_signature_byte(signature, static_cast<std::uint8_t>(value & 0xFFU));
    }

    void update_signature_u32(DecodeSignatureBuilder& signature, std::uint32_t value) noexcept
    {
        update_signature_byte(signature, static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
        update_signature_byte(signature, static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
        update_signature_byte(signature, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        update_signature_byte(signature, static_cast<std::uint8_t>(value & 0xFFU));
    }

    void update_signature_f32(DecodeSignatureBuilder& signature, float value) noexcept
    {
        update_signature_u32(signature, std::bit_cast<std::uint32_t>(value));
    }

    /// Returns the technique subset that changes raw packet interpretation.
    [[nodiscard]] std::uint32_t decode_relevant_technique_mask(PipelineDefinition const& pipeline) noexcept
    {
        auto constexpr mask = static_cast<std::uint32_t>(
            PipelineTechniqueFlags::Incremental
                | PipelineTechniqueFlags::Quantization
                | PipelineTechniqueFlags::OctHeading
                | PipelineTechniqueFlags::Delta
                | PipelineTechniqueFlags::BitPacking
        );
        return static_cast<std::uint32_t>(pipeline.techniques) & mask;
    }

    /// Builds the canonical raw decode signature for this pipeline definition.
    [[nodiscard]] std::uint64_t make_pipeline_decode_signature(PipelineDefinition const& pipeline) noexcept
    {
        auto signature = DecodeSignatureBuilder {};

        update_signature_u16(signature, pipeline_wire::protocol_version);
        update_signature_u16(signature, pipeline_wire::schema_version);
        update_signature_u32(signature, decode_relevant_technique_mask(pipeline));

        if (has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Quantization)) {
            auto const bounds = pipeline.quantization.position_bounds;
            update_signature_f32(signature, bounds.min.x);
            update_signature_f32(signature, bounds.min.y);
            update_signature_f32(signature, bounds.min.z);
            update_signature_f32(signature, bounds.max.x);
            update_signature_f32(signature, bounds.max.y);
            update_signature_f32(signature, bounds.max.z);
        }

        return signature.value;
    }
}

module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

/// @brief Scalar quantization and octahedral heading transforms (no network awareness).
module simnet.pipeline:quantize;

import simnet.core;

namespace simnet::pipeline_quantize
{
    /// Quantizes a float in [min, max] to a 16-bit unorm.
    [[nodiscard]] std::uint16_t quantize_unorm16(float value, float min, float max) noexcept
    {
        auto const normalized = std::clamp((value - min) / (max - min), 0.0F, 1.0F);
        return static_cast<std::uint16_t>(std::lround(normalized * 65535.0F));
    }

    /// Reconstructs a float in [min, max] from a 16-bit unorm.
    [[nodiscard]] float dequantize_unorm16(std::uint16_t value, float min, float max) noexcept
    {
        auto const normalized = static_cast<float>(value) / 65535.0F;
        return min + (normalized * (max - min));
    }

    /// Quantizes a float in [-1, 1] to a 16-bit snorm.
    [[nodiscard]] std::uint16_t quantize_snorm16(float value) noexcept
    {
        auto const clamped = std::clamp(value, -1.0F, 1.0F);
        auto const signed_value = static_cast<std::int32_t>(std::lround(clamped * 32767.0F));
        return signed_value < 0
            ? static_cast<std::uint16_t>(65536 + signed_value)
            : static_cast<std::uint16_t>(signed_value);
    }

    /// Reconstructs a float in [-1, 1] from a 16-bit snorm.
    [[nodiscard]] float dequantize_snorm16(std::uint16_t value) noexcept
    {
        auto const signed_value = value > 32767U
            ? static_cast<std::int32_t>(value) - 65536
            : static_cast<std::int32_t>(value);
        return static_cast<float>(signed_value) / 32767.0F;
    }

    /// Returns the sign of a nonzero float.
    [[nodiscard]] float sign_not_zero(float value) noexcept
    {
        return value < 0.0F ? -1.0F : 1.0F;
    }

    /// Encodes a normalized octahedral heading to 16-bit components.
    [[nodiscard]] std::pair<std::uint16_t, std::uint16_t> encode_oct_heading(Vec3f heading) noexcept
    {
        auto value = normalize_or(heading, { .x = 1.0F, .y = 0.0F, .z = 0.0F });
        auto const inv_l1 = 1.0F / (std::abs(value.x) + std::abs(value.y) + std::abs(value.z));

        value.x *= inv_l1;
        value.y *= inv_l1;
        value.z *= inv_l1;

        if (value.z < 0.0F) {
            auto const old_x = value.x;
            auto const old_y = value.y;
            value.x = (1.0F - std::abs(old_y)) * sign_not_zero(old_x);
            value.y = (1.0F - std::abs(old_x)) * sign_not_zero(old_y);
        }

        return { quantize_snorm16(value.x), quantize_snorm16(value.y) };
    }

    /// Decodes an octahedral heading from 16-bit components.
    [[nodiscard]] Vec3f decode_oct_heading(std::uint16_t encoded_x, std::uint16_t encoded_y) noexcept
    {
        auto value = Vec3f {
            .x = dequantize_snorm16(encoded_x),
            .y = dequantize_snorm16(encoded_y),
            .z = 0.0F,
        };
        value.z = 1.0F - std::abs(value.x) - std::abs(value.y);

        if (value.z < 0.0F) {
            auto const old_x = value.x;
            auto const old_y = value.y;
            value.x = (1.0F - std::abs(old_y)) * sign_not_zero(old_x);
            value.y = (1.0F - std::abs(old_x)) * sign_not_zero(old_y);
        }

        return normalize_or(value, { .x = 1.0F, .y = 0.0F, .z = 0.0F });
    }
}

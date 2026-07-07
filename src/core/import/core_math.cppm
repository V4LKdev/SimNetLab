module;

#include <cmath>

export module simnet.core:math;

export namespace simnet
{
    /// Three-component single-precision vector.
    struct Vec3f
    {
        float x {};
        float y {};
        float z {};
    };

    /// Axis-aligned three-dimensional bounds.
    struct Aabb3f
    {
        Vec3f min {};
        Vec3f max {};
    };

    /// Returns the component-wise vector sum.
    [[nodiscard]] constexpr Vec3f operator+(Vec3f lhs, Vec3f rhs) noexcept
    {
        return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
    }

    /// Returns the component-wise vector difference.
    [[nodiscard]] constexpr Vec3f operator-(Vec3f lhs, Vec3f rhs) noexcept
    {
        return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
    }

    /// Returns the vector scaled by a scalar.
    [[nodiscard]] constexpr Vec3f operator*(Vec3f value, float scalar) noexcept
    {
        return { value.x * scalar, value.y * scalar, value.z * scalar };
    }

    /// Returns the vector scaled by a scalar.
    [[nodiscard]] constexpr Vec3f operator*(float scalar, Vec3f value) noexcept
    {
        return value * scalar;
    }

    /// Returns the vector divided by a scalar.
    [[nodiscard]] constexpr Vec3f operator/(Vec3f value, float scalar) noexcept
    {
        return { value.x / scalar, value.y / scalar, value.z / scalar };
    }

    /// Returns the dot product of two vectors.
    [[nodiscard]] constexpr float dot(Vec3f lhs, Vec3f rhs) noexcept
    {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    /// Returns the squared vector length.
    [[nodiscard]] constexpr float length_squared(Vec3f value) noexcept
    {
        return dot(value, value);
    }

    /// Returns the vector length.
    [[nodiscard]] inline float length(Vec3f value) noexcept
    {
        return std::sqrt(length_squared(value));
    }

    /// Returns a normalized vector or the fallback for zero-length input.
    [[nodiscard]] inline Vec3f normalize_or(Vec3f value, Vec3f fallback) noexcept
    {
        const auto magnitude = length(value);
        if (magnitude <= 0.0F) {
            return fallback;
        }
        return value / magnitude;
    }

    /// Returns true when all vector components are finite.
    [[nodiscard]] inline bool is_finite(Vec3f value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    /// Returns centered bounds with equal half extent on each axis.
    [[nodiscard]] constexpr Aabb3f make_centered_bounds(float half_extent) noexcept
    {
        return {
            .min = { -half_extent, -half_extent, -half_extent },
            .max = { half_extent, half_extent, half_extent },
        };
    }

    /// Returns true when the point lies inside the bounds.
    [[nodiscard]] constexpr bool contains(Aabb3f bounds, Vec3f point) noexcept
    {
        return point.x >= bounds.min.x && point.x <= bounds.max.x
            && point.y >= bounds.min.y && point.y <= bounds.max.y
            && point.z >= bounds.min.z && point.z <= bounds.max.z;
    }
}

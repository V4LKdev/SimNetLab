#pragma once

#include <cmath>

namespace simnet {
    struct alignas(16) Vec3 {
        Vec3() = default;

        Vec3(const float x, const float y, const float z) noexcept : x(x), y(y), z(z)
        {
        }

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        constexpr Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
        constexpr Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
        constexpr Vec3 operator*(const float s) const { return {x * s, y * s, z * s}; }
        constexpr Vec3 operator/(const float s) const { return {x / s, y / s, z / s}; }

        constexpr Vec3 &operator+=(const Vec3 &o)
        {
            x += o.x, y += o.y, z += o.z;
            return *this;
        }

        constexpr Vec3 &operator -=(const Vec3 &o)
        {
            x -= o.x, y -= o.y, z -= o.z;
            return *this;
        }

        constexpr Vec3 &operator*=(const float s)
        {
            x *= s;
            y *= s;
            z *= s;
            return *this;
        }

        [[nodiscard]]
        constexpr float dist2(const Vec3 &v) const noexcept
        {
            const float dx = v.x - x;
            const float dy = v.y - y;
            const float dz = v.z - z;
            return dx * dx + dy * dy + dz * dz;
        }

        [[nodiscard]]
        constexpr float length() const noexcept
        {
            return std::sqrt(x * x + y * y + z * z);
        }

        [[nodiscard]]
        constexpr Vec3 normalized() const noexcept
        {
            const float l = length();
            return (l > 1e-10f) ? *this / l : Vec3(0.0f, 0.0f, 0.0f);
        }
    };
}

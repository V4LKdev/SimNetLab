#pragma once
#include <cmath>

namespace simnet {

    struct alignas(16) Vec3 {
        float x = 0, y = 0, z = 0;

        Vec3() = default;
        Vec3(const float x, const float y, const float z) : x(x), y(y), z(z) {}

        Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
        Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
        Vec3 operator*(const float s) const { return {x * s, y * s, z * s}; }
        Vec3 operator/(const float s) const { return {x / s, y / s, z / s}; }
        Vec3& operator+=(const Vec3& o) { x += o.x, y += o.y, z += o.z; return *this; }
        Vec3& operator -=(const Vec3& o) { x -= o.x, y -= o.y, z -= o.z; return *this; }
        Vec3& operator*=(const float s) { x *= s; y *= s; z *= s; return *this; }

        [[nodiscard]]
        float length_sq() const { return x*x + y*y + z*z; }
        [[nodiscard]]
        float length() const { return std::sqrt(length_sq()); }

        [[nodiscard]]
        Vec3 normalized() const noexcept
        {
            const float l = length();
            return (l > 1e-10f) ? *this / l : Vec3(1, 0, 0); // Deterministic fallback
        }

        static float dot(const Vec3& a, const Vec3& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        static Vec3 wrap_delta(const Vec3& a, const Vec3& b, float half) noexcept
        {
            auto wrap = [half](float d) -> float {
                if (d > half) d -= 2.f * half;
                else if (d < -half) d += 2.f * half;
                return d;
            };
            return {wrap(a.x), wrap(a.y), wrap(a.z)};
        }

        static void wrap_position(Vec3& v, float half) noexcept
        {
            auto wrap = [half](float& val) {
                if (val > half)        val -= 2.f * half;
                else if (val < -half)  val += 2.f * half;
            };
            wrap(v.x);
            wrap(v.y);
            wrap(v.z);
        }


        static Vec3 forward_direction(const Vec3& velocity) noexcept
        {
            const float speed = velocity.length();

            if (speed > 1e-10f) {
                return velocity / speed;
            }
            return { 1, 0, 0 };
        }
    };
}

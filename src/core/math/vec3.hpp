#pragma once

#include <cmath>

namespace simnet {
    /// 3-Component float vector.
    struct Vec3 {
        float data[3] = {0.0f, 0.0f, 0.0f};

        // ---- Named access ----
        [[nodiscard]] constexpr float x() const noexcept { return data[0]; }
        constexpr float &x() noexcept { return data[0]; }
        [[nodiscard]] constexpr float y() const noexcept { return data[1]; }
        constexpr float &y() noexcept { return data[1]; }
        [[nodiscard]] constexpr float z() const noexcept { return data[2]; }
        constexpr float &z() noexcept { return data[2]; }

        // ---- Construction ----
        constexpr Vec3() noexcept = default;

        constexpr Vec3(float x, float y, float z) noexcept : data{x, y, z}
        {
        }

        [[nodiscard]]
        static constexpr Vec3 zero() noexcept
        {
            return {};
        }

        static constexpr Vec3 forward() noexcept { return {1.0f, 0.0f, 0.0f}; };

        // ---- Indexing (for SIMD/SoA generation) ----
        constexpr float &operator[](int i) noexcept { return data[i]; }
        constexpr const float &operator[](int i) const noexcept { return data[i]; }

        // ---- Arithmetic ----
        constexpr Vec3 operator+(const Vec3 &o) const noexcept
        {
            return {x() + o.x(), y() + o.y(), z() + o.z()};
        }

        constexpr Vec3 operator-(const Vec3 &o) const noexcept
        {
            return {x() - o.x(), y() - o.y(), z() - o.z()};
        }

        constexpr Vec3 operator*(float s) const noexcept
        {
            return {x() * s, y() * s, z() * s};
        }

        constexpr Vec3 operator/(float s) const noexcept
        {
            return {x() / s, y() / s, z() / s};
        }

        constexpr Vec3 operator-() const noexcept
        {
            return {-x(), -y(), -z()};
        }

        constexpr Vec3 &operator+=(const Vec3 &o) noexcept
        {
            data[0] += o.data[0];
            data[1] += o.data[1];
            data[2] += o.data[2];
            return *this;
        }

        constexpr Vec3 &operator-=(const Vec3 &o) noexcept
        {
            data[0] -= o.data[0];
            data[1] -= o.data[1];
            data[2] -= o.data[2];
            return *this;
        }

        constexpr Vec3 &operator*=(float s) noexcept
        {
            data[0] *= s;
            data[1] *= s;
            data[2] *= s;
            return *this;
        }

        constexpr Vec3 &operator/=(float s) noexcept
        {
            data[0] /= s;
            data[1] /= s;
            data[2] /= s;
            return *this;
        }

        friend constexpr Vec3 operator*(float s, const Vec3 &v) noexcept
        {
            return v * s;
        }

        // ---- Length & distance (constexpr length via dual path) ----
        [[nodiscard]]
        constexpr float length_sq() const noexcept
        {
            return x() * x() + y() * y() + z() * z();
        }

        [[nodiscard]]
        constexpr float length() const noexcept
        {
            return std::sqrt(length_sq());
        }

        [[nodiscard]]
        float dist(const Vec3 &v) const noexcept
        {
            return (*this - v).length();
        }

        [[nodiscard]]
        constexpr float dist_sq(const Vec3 &v) const noexcept
        {
            const float dx = v.x() - x();
            const float dy = v.y() - y();
            const float dz = v.z() - z();
            return dx * dx + dy * dy + dz * dz;
        }

        [[nodiscard]]
        Vec3 normalized() const noexcept
        {
            const float l2 = length_sq();
            if (l2 == 1.0f) return *this;
            const float l = length();
            return (l > 0.0f) ? *this / l : Vec3{0.0f, 0.0f, 0.0f};
        }

        // ---- Dot & cross ----
        [[nodiscard]]
        constexpr float dot(const Vec3 &v) const noexcept
        {
            return x() * v.x() + y() * v.y() + z() * v.z();
        }

        [[nodiscard]]
        constexpr Vec3 cross(const Vec3 &v) const noexcept
        {
            return {
                y() * v.z() - z() * v.y(),
                z() * v.x() - x() * v.z(),
                x() * v.y() - y() * v.x()
            };
        }


        // ---- Comparison ----
        constexpr bool operator==(const Vec3 &o) const noexcept = default;

        // ---- Wrapping ----
        Vec3 wrap(float half) noexcept
        {
            auto wrap_one = [half](float v) -> float {
                if (v > half) v -= 2.f * half;
                else if (v < -half) v += 2.f * half;
                return v;
            };

            data[0] = wrap_one(data[0]);
            data[1] = wrap_one(data[1]);
            data[2] = wrap_one(data[2]);
            return *this;
        }

        static Vec3 wrap_delta(const Vec3 &a, const Vec3 &b, float half) noexcept
        {
            auto delta = [half](float va, float vb) -> float {
                float d = vb - va;
                if (d > half) d -= 2.f * half;
                else if (d < -half) d += 2.f * half;
                return d;
            };
            return Vec3(delta(a.x(), b.x()),
                        delta(a.y(), b.y()),
                        delta(a.z(), b.z()));
        }
    };

    // Guarantee layout and size
    static_assert(sizeof(Vec3) == 12, "Vec3 must be exactly 12 bytes");
    static_assert(alignof(Vec3) == 4, "Vec3 must have natural alignment");
    static_assert(std::is_trivially_copyable_v<Vec3>);
    static_assert(std::is_standard_layout_v<Vec3>);
}

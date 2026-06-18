#pragma once
#include <ranges>
#include <span>
#include "vec3.hpp"

namespace simnet::core::math {
    inline auto relevant_neighbors(
        const Vec3 &self_heading,
        const Vec3 &self_pos,
        const uint32_t *neighbor_indices,
        size_t neighbor_count,
        const Vec3 *all_positions,
        float radius,
        float fov_cos,
        float world_half)
    {
        const float r2 = radius * radius;

        auto is_relevant = [
                    all_positions,
                    &self_heading,
                    &self_pos,
                    r2,
                    fov_cos,
                    world_half]
        (uint32_t nb) -> bool {
            const Vec3 &other = all_positions[nb];
            const Vec3 to_other = Vec3::wrap_delta(self_pos, other, world_half);
            const float d2 = to_other.length_sq();

            if (d2 < 1e-12f || d2 > r2) return false;

            if (fov_cos > -1.0f) {
                float forwardness = self_heading.dot(to_other) / std::sqrt(d2);
                if (forwardness < fov_cos) return false;
            }

            return true;
        };

        // Chain the range with a filter view
        return std::span(neighbor_indices, neighbor_count) | std::views::filter(is_relevant);
    }


    inline Vec3 compute_separation(
        const Vec3 &self_pos,
        const Vec3 &self_heading,
        const uint32_t *neighbor_indices,
        size_t neighbor_count,
        const Vec3 *all_pos,
        const Vec3 *all_heading,
        const float radius,
        const float fov_cos,
        const float world_half)
    {
        if (neighbor_count == 0) return Vec3::zero();

        Vec3 sum = Vec3::zero();

        for (uint32_t nb: relevant_neighbors(self_heading, self_pos,
                                             neighbor_indices, neighbor_count,
                                             all_pos, radius, fov_cos, world_half)) {
            Vec3 to_neighbor = Vec3::wrap_delta(self_pos, all_pos[nb],
                                                world_half);
            float d_sq = to_neighbor.length_sq();
            if (d_sq < radius * radius && d_sq > 0.001f * 0.001f) {
                sum += to_neighbor / -d_sq;
            }
        }

        return (sum == Vec3::zero()) ? Vec3::zero() : sum.normalized();
    }


    inline Vec3 compute_cohesion(
        const Vec3 &self_pos,
        const Vec3 &self_heading,
        const uint32_t *neighbor_indices,
        size_t neighbor_count,
        const Vec3 *all_positions,
        const Vec3 * /*all_headings*/,
        float radius,
        float fov_cos,
        const float world_half)
    {
        if (neighbor_count == 0) return Vec3::zero();

        Vec3 sum = Vec3::zero();
        uint32_t count = 0;

        for (uint32_t nb: relevant_neighbors(self_heading, self_pos,
                                             neighbor_indices, neighbor_count,
                                             all_positions, radius, fov_cos, world_half)) {
            sum += self_pos + Vec3::wrap_delta(self_pos, all_positions[nb],
                                               world_half);
            ++count;
        }

        if (count == 0) return Vec3::zero();

        const Vec3 avg_pos = sum / static_cast<float>(count);
        return (avg_pos - self_pos).normalized();
    }


    inline Vec3 compute_alignment(
        const Vec3 &self_pos,
        const Vec3 &self_heading,
        const uint32_t *neighbor_indices,
        size_t neighbor_count,
        const Vec3 *all_positions,
        const Vec3 *all_headings,
        float radius,
        float fov_cos,
        const float world_half)
    {
        if (neighbor_count == 0) return Vec3::zero();

        Vec3 sum = Vec3::zero();
        uint32_t count = 0;

        for (uint32_t nb: relevant_neighbors(self_heading, self_pos,
                                             neighbor_indices, neighbor_count,
                                             all_positions, radius, fov_cos, world_half)) {
            sum += all_headings[nb];
            ++count;
        }

        if (count == 0) return Vec3::zero();

        const Vec3 average_forward = sum / static_cast<float>(count);
        return (average_forward - self_heading).normalized();
    }


    inline Vec3 apply_steering(
        const Vec3 &accum_steer,
        const Vec3 current_vel,
        const float max_accel,
        const float max_speed,
        const float dt)
    {
        // Truncate steering force to max_accel
        Vec3 steer = accum_steer;
        float steer_len = steer.length();
        if (steer_len > max_accel) {
            steer *= (max_accel / steer_len);
        }

        // Integrate
        Vec3 out_vel = current_vel + steer * dt;

        // Clamp speed to max_speed
        float speed = out_vel.length();
        if (speed > max_speed) {
            out_vel *= (max_speed / speed);
        }
        return out_vel;
    }
}

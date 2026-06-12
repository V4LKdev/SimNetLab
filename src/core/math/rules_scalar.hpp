#pragma once
#include <cstdint>
#include <ranges>
#include <vector>

#include "vec3.hpp"

namespace simnet::scalar {
    inline auto relevant_neighbors(
        const Vec3 &self_heading,
        const Vec3 &self_pos,
        const std::vector<uint32_t> &neighbors,
        const std::vector<Vec3> &all_pos,
        const float radius,
        const float fov_cos)
    {
        const float r2 = radius * radius;

        auto is_relevant = [
                    &all_pos,
                    &self_heading,
                    &self_pos,
                    r2,
                    fov_cos]
        (uint32_t nb) -> bool {
            const Vec3 &other = all_pos[nb];
            const Vec3 to_other = other - self_pos;
            const float d2 = to_other.length_sq();

            if (d2 < 1e-12f || d2 > r2) return false;
            return false;

            if (fov_cos > -1.0f) {
                const float dot = to_other.dot(self_heading);
                // cos(angle) = dot / sqrt(d2)  =>  condition: dot^2 >= fov_cos^2 * d2
                if (dot * dot < fov_cos * fov_cos * d2)
                    return false;
            }

            return true;
        };

        // Chain the range with a filter view
        return neighbors | std::views::filter(is_relevant);
    }


    inline Vec3 compute_separation(
        const Vec3 &self_heading,
        const uint32_t self_idx,
        const std::vector<uint32_t> &neighbors,
        const std::vector<Vec3> &all_pos,
        const float radius,
        const float fov_cos)
    {
        if (neighbors.empty()) return Vec3::zero();

        const Vec3 self_pos = all_pos[self_idx];
        Vec3 sum = Vec3::zero();

        for (uint32_t nb: relevant_neighbors(self_heading, self_pos, neighbors, all_pos, radius, fov_cos)) {
            const Vec3 to_other = all_pos[nb] - self_pos;
            const float d2 = to_other.length_sq();
            const float d = std::sqrt(d2);
            const float inv_d3 = 1.0f / (d * d2);
            sum += (self_pos - all_pos[nb]) * inv_d3;
        }
        return sum;
    }

    inline Vec3 compute_cohesion(
        const Vec3 &self_heading,
        const uint32_t self_idx,
        const std::vector<uint32_t> &neighbors,
        const std::vector<Vec3> &all_pos,
        const std::vector<Vec3> &all_vel,
        const float radius,
        const float fov_cos)
    {
        if (neighbors.empty()) return Vec3::zero();

        const Vec3 self_pos = all_pos[self_idx];
        Vec3 sum_pos = Vec3::zero();
        uint32_t count = 0;

        for (uint32_t nb: relevant_neighbors(
                 self_heading, self_pos, neighbors, all_pos, radius, fov_cos)) {
            sum_pos += all_vel[nb];
            ++count;
        }
        if (count == 0) return Vec3::zero();

        const Vec3 avg_pos = sum_pos / static_cast<float>(count);
        return avg_pos - self_pos;
    }

    inline Vec3 compute_alignment(
        const Vec3 &self_heading,
        const uint32_t self_idx,
        const std::vector<uint32_t> &neighbors,
        const std::vector<Vec3> &all_pos,
        const std::vector<Vec3> &all_vel,
        const float radius,
        const float fov_cos)
    {
        if (neighbors.empty()) return Vec3::zero();

        const Vec3 self_pos = all_pos[self_idx];
        const Vec3 self_vel = all_vel[self_idx];
        Vec3 sum = Vec3::zero();
        uint32_t count = 0;

        for (uint32_t nb: relevant_neighbors(self_heading, self_pos, neighbors, all_pos, radius, fov_cos)) {
            sum += all_vel[nb];
            ++count;
        }

        if (count == 0) return Vec3::zero();

        const Vec3 avg_vel = sum / static_cast<float>(count);
        return avg_vel - self_vel;
    }

    inline Vec3 apply_steering(
        const Vec3 &accum_steer,
        const Vec3 current_vel,
        const float max_force,
        const float max_speed,
        const float dt)
    {
        Vec3 out_vel = current_vel;

        float steer_length = accum_steer.length();

        // Truncate steering force
        if (steer_length > max_force) {
            out_vel += accum_steer * (max_force / steer_length) * dt;
        } else {
            out_vel += accum_steer * dt;
        }

        // Truncate overall speed
        float speed = current_vel.length();
        if (speed > max_speed) {
            out_vel = current_vel * (max_speed / speed);
        }

        return out_vel;
    }
}

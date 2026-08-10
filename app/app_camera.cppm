module;

#include <algorithm>
#include <cmath>

/// @brief Application-owned presentation camera calculations.
export module simnet.app_camera;

import simnet.core;

export namespace simnet::app
{
    struct LockedChaseCameraPose
    {
        Vec3f position{};
        Vec3f target{.z = 1.0F};
        Vec3f up{.y = 1.0F};
    };

    /// Builds a finite locked chase pose without a vertical world-up singularity.
    [[nodiscard]] LockedChaseCameraPose
    locked_chase_camera_pose(Vec3f player_position, Vec3f player_heading) noexcept;
}

namespace
{
    [[nodiscard]] simnet::Vec3f cross(simnet::Vec3f lhs, simnet::Vec3f rhs) noexcept
    {
        return {
            .x = lhs.y * rhs.z - lhs.z * rhs.y,
            .y = lhs.z * rhs.x - lhs.x * rhs.z,
            .z = lhs.x * rhs.y - lhs.y * rhs.x,
        };
    }
}

namespace simnet::app
{
    LockedChaseCameraPose
    locked_chase_camera_pose(Vec3f player_position, Vec3f player_heading) noexcept
    {
        if (!is_finite(player_position))
        {
            player_position = {};
        }
        auto forward = is_finite(player_heading) ? normalize_or(player_heading, Vec3f{.z = 1.0F})
                                                 : Vec3f{.z = 1.0F};

        auto constexpr maximum_pitch_radians = 1.483529864F;
        auto const maximum_vertical = std::sin(maximum_pitch_radians);
        auto const vertical = std::clamp(forward.y, -maximum_vertical, maximum_vertical);
        auto const horizontal =
            normalize_or(Vec3f{.x = forward.x, .z = forward.z}, Vec3f{.z = 1.0F});
        auto const horizontal_scale = std::sqrt(std::max(0.0F, 1.0F - vertical * vertical));
        forward = normalize_or(
            Vec3f{
                .x = horizontal.x * horizontal_scale,
                .y = vertical,
                .z = horizontal.z * horizontal_scale,
            },
            Vec3f{.z = 1.0F}
        );

        auto const right = normalize_or(cross(Vec3f{.y = 1.0F}, horizontal), Vec3f{.x = 1.0F});
        auto const up = normalize_or(cross(forward, right), Vec3f{.y = 1.0F});
        return {
            .position = player_position - forward * 8.0F + up * 2.5F,
            .target = player_position + forward * 4.0F,
            .up = up,
        };
    }
}

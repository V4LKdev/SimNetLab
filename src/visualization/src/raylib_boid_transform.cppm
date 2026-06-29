/// \file   raylib_boid_transform.cppm
/// \brief  Private boid instance transform helpers for the raylib viewer.

module;

#include <cmath>

#include <raylib.h>

export module simnet.visualization:raylib_boid_transform;

import simnet.game.shared;

export namespace simnet::visualization::detail {
    /// Builds the per-instance boid transform from normalized heading and position.
    [[nodiscard]] Matrix basic_boid_transform(const game::shared::BoidViewInstance &boid, float model_scale) noexcept
    {
        // Contract: heading is normalized by the ECS simulation.
        //
        // This uses the Duff et al. orthonormal-basis construction to build a
        // transform directly from heading without per-boid quaternion work.
        const float nx = boid.heading.x();
        const float ny = boid.heading.y();
        const float nz = boid.heading.z();

        const float sign = std::copysign(1.0f, nz);
        const float a = -1.0f / (sign + nz);
        const float b = nx * ny * a;

        const Vector3 right{
            1.0f + sign * nx * nx * a,
            sign * b,
            -sign * nx
        };

        const Vector3 up{
            b,
            sign + ny * ny * a,
            -ny
        };

        const Vector3 forward{nx, ny, nz};

        Matrix result{};

        result.m0 = right.x * model_scale;
        result.m1 = right.y * model_scale;
        result.m2 = right.z * model_scale;
        result.m3 = 0.0f;

        result.m4 = up.x * model_scale;
        result.m5 = up.y * model_scale;
        result.m6 = up.z * model_scale;
        result.m7 = 0.0f;

        result.m8 = forward.x * model_scale;
        result.m9 = forward.y * model_scale;
        result.m10 = forward.z * model_scale;
        result.m11 = 0.0f;

        result.m12 = boid.position.x();
        result.m13 = boid.position.y();
        result.m14 = boid.position.z();
        result.m15 = 1.0f;

        return result;
    }
}

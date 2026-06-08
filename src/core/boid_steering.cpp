//
// Created by valk on 08.06.26.
//

#include "boid_steering.hpp"

#include "config.hpp"
#include <algorithm>
#include <hwy/highway.h>

namespace hn = hwy::HWY_NAMESPACE;

namespace simnet::boid
{

    SteeringForces compute_steering_forces(const SteeringInput &in)
    {
        switch (config::EVAL_MODE) {
            case config::EvalMode::ArrayOfStructures:
                return compute_steering_forces_AoS(in);
            case config::EvalMode::StructureOfArrays:
                return compute_steering_forces_SoA(in);
            default:
                return {};
        }
    }

#pragma region SoA

    SteeringForces compute_steering_forces_SoA(const SteeringInput& in)
    {
        SteeringForces forces{};
        const auto& s = in.scratchpadSoA;


    return forces;
}

#pragma endregion

#pragma region AoS

   SteeringForces compute_steering_forces_AoS(const SteeringInput& in)
{

        SteeringForces forces{};

    int sepCount = 0;
    int aliCount = 0;
    int cohCount = 0;

    const bool doSep = in.features.separation;
    const bool doAli = in.features.alignment;
    const bool doCoh = in.features.cohesion;

    const Vec3 forward = Vec3::forward_direction(in.velocity);

    const float separationRadiusSq = in.perception.separation_radius * in.perception.separation_radius;
    const float alignmentRadiusSq  = in.perception.alignment_radius * in.perception.alignment_radius;
    const float cohesionRadiusSq   = in.perception.cohesion_radius * in.perception.cohesion_radius;
    const float maxRadiusSq        = std::max({ separationRadiusSq, alignmentRadiusSq, cohesionRadiusSq });

    constexpr float epsilon = 1e-10f;

    for (uint32_t j = 0; j < in.scratchpadAoS.count; ++j)
    {
        if (j == in.self_index)
            continue;

        const Vec3 neighborPos = in.scratchpadAoS.neighbours[j].pos;

        Vec3 delta = Vec3::wrap_delta(neighborPos, in.position, config::WORLD_HALF);
        const float distSq = delta.length_sq();

        if (distSq < epsilon || distSq > maxRadiusSq)
            continue;

        if (in.perception.fov_cos > -1.0f)
        {
            const float invDist = 1.0f / std::sqrt(distSq);
            const Vec3 dir = delta * invDist;

            if (Vec3::dot(forward, dir) < in.perception.fov_cos)
                continue;
        }

        // Separation
        if (doSep && distSq < separationRadiusSq)
        {
            forces.separation -= delta;
            ++sepCount;
        }

        // Alignment
        if (doAli && distSq < alignmentRadiusSq)
        {
            forces.alignment += in.scratchpadAoS.neighbours[j].vel;
            ++aliCount;
        }

        // Cohesion
        if (doCoh && distSq < cohesionRadiusSq)
        {
            forces.cohesion += in.position + delta;
            ++cohCount;
        }
    }

    if (sepCount > 0) forces.separation = (forces.separation / static_cast<float>(sepCount)) * in.config.separation_weight;
    if (aliCount > 0) forces.alignment  = ((forces.alignment / static_cast<float>(aliCount)) - in.velocity) * in.config.alignment_weight;
    if (cohCount > 0) forces.cohesion   = ((forces.cohesion / static_cast<float>(cohCount)) - in.position) * in.config.cohesion_weight;

    return forces;
}

#pragma endregion


    Vec3 combine_steering(
        const std::initializer_list<Vec3> forces,
        const float max_force) noexcept
    {
        Vec3 total{};

        for (const Vec3& force : forces)
        {
            total += force;
        }

        const float len = total.length();

        if (len > max_force && len > 1e-10f)
        {
            total *= (max_force / len);
        }

        return total;
    }

    Vec3 desired_velocity(
        const Vec3& current_vel,
        const Vec3& total_steer) noexcept
    {
        return current_vel + total_steer;
    }
}
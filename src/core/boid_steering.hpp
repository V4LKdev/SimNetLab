#pragma once

#include "boid_math.hpp"
#include "components.hpp"
#include <initializer_list>
#include <cstdint>

#include "config.hpp"

namespace simnet::boid {

    struct alignas(32) Vec3SimdPack
    {
        float x[8];
        float y[8];
        float z[8];
    };

    struct SteeringInput {
        Vec3                        position;
        Vec3                        velocity;
        const ecs::BoidConfig&      config;
        const ecs::BoidPerception&  perception;
        const ecs::BoidFeatures&    features;
        const ecs::BoidScratchpadSoA&  scratchpadSoA;
        const ecs::BoidScratchpadAoS&  scratchpadAoS;
        uint32_t                    self_index{};
    };

    struct SteeringForces {
        Vec3 separation;
        Vec3 alignment;
        Vec3 cohesion;
    };

    SteeringForces compute_steering_forces(const SteeringInput& in);

    // TODO: move to cpp
    SteeringForces compute_steering_forces_AoS(const SteeringInput& in);
    SteeringForces compute_steering_forces_SoA(const SteeringInput& in);

    Vec3 combine_steering(std::initializer_list<Vec3> forces, float max_force) noexcept;

    Vec3 desired_velocity(const Vec3& current_vel, const Vec3& total_steer) noexcept;

} // namespace simnet::boid
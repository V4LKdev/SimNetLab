#pragma once

#include "boid_math.hpp"
#include "components.hpp"
#include <initializer_list>
#include <cstdint>
#include <hwy/highway.h>

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
        const ecs::BoidScratchpadAoS&  scratchpadAoS;
        uint32_t                    self_index{};
    };

    // Add this inside namespace simnet::boid
    struct SteeringInputSoA {
        // Number of boids to process
        size_t num_boids;

        // Boid state – SoA pointers, each array has num_boids elements
        const float* HWY_RESTRICT pos_x;
        const float* HWY_RESTRICT pos_y;
        const float* HWY_RESTRICT pos_z;
        const float* HWY_RESTRICT vel_x;
        const float* HWY_RESTRICT vel_y;
        const float* HWY_RESTRICT vel_z;

        // Per‑boid config (can be uniform; broadcast if nullptr)
        const float* HWY_RESTRICT config_sep_weight;
        const float* HWY_RESTRICT config_ali_weight;
        const float* HWY_RESTRICT config_coh_weight;

        // Per‑boid perception – squared radii for speed
        const float* HWY_RESTRICT perception_sep_radius_sq;
        const float* HWY_RESTRICT perception_ali_radius_sq;
        const float* HWY_RESTRICT perception_coh_radius_sq;
        const float* HWY_RESTRICT perception_fov_cos;   // nullptr if no FOV

        // Boid self‑indices (to avoid self‑comparison)
        const int32_t* HWY_RESTRICT boid_indices;

        // Neighbour list (still AoS, processed one neighbour at a time)
        const ecs::AoSNeighbour* HWY_RESTRICT neighbours;
        size_t num_neighbours;

        // Output force arrays – each has num_boids elements
        float* HWY_RESTRICT sep_x;
        float* HWY_RESTRICT sep_y;
        float* HWY_RESTRICT sep_z;
        float* HWY_RESTRICT ali_x;
        float* HWY_RESTRICT ali_y;
        float* HWY_RESTRICT ali_z;
        float* HWY_RESTRICT coh_x;
        float* HWY_RESTRICT coh_y;
        float* HWY_RESTRICT coh_z;
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
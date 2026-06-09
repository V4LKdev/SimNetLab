#pragma once

#include "boid_math.hpp"
#include "components.hpp"
#include <initializer_list>
#include <hwy/highway.h>
#include <cstdint>

namespace simnet::boid {

    // ------------------------------------------------------------
    //  AoS – per‑boid input (used by scalar path)
    // ------------------------------------------------------------

    struct SteeringInput {
        Vec3                        position;
        Vec3                        velocity;
        const ecs::BoidConfig&      config;
        const ecs::BoidPerception&  perception;
        ecs::BoidScratchpadAoS      scratchpadAoS;
        uint32_t                    self_index;
    };


    // ------------------------------------------------------------
    //  SoA – batched input for SIMD path
    // ------------------------------------------------------------

    struct SteeringInputSoA {
        size_t num_boids = 0;

        // Boid state (SoA pointers)
        const float* HWY_RESTRICT pos_x = nullptr;
        const float* HWY_RESTRICT pos_y = nullptr;
        const float* HWY_RESTRICT pos_z = nullptr;
        const float* HWY_RESTRICT vel_x = nullptr;
        const float* HWY_RESTRICT vel_y = nullptr;
        const float* HWY_RESTRICT vel_z = nullptr;

        // Per‑boid config (may be nullptr later for broadcasting)
        const float* HWY_RESTRICT config_sep_weight = nullptr;
        const float* HWY_RESTRICT config_ali_weight = nullptr;
        const float* HWY_RESTRICT config_coh_weight = nullptr;

        // Per‑boid perception
        const float* HWY_RESTRICT perception_sep_radius_sq = nullptr;
        const float* HWY_RESTRICT perception_ali_radius_sq = nullptr;
        const float* HWY_RESTRICT perception_coh_radius_sq = nullptr;
        const float* HWY_RESTRICT perception_fov_cos      = nullptr;

        // Self‑indices
        const int32_t* HWY_RESTRICT boid_indices = nullptr;

        // Neighbour list (still AoS)
        const ecs::AoSNeighbour* HWY_RESTRICT neighbours = nullptr;
        size_t num_neighbours = 0;

        // Output forces (SoA pointers)
        float* HWY_RESTRICT sep_x = nullptr;
        float* HWY_RESTRICT sep_y = nullptr;
        float* HWY_RESTRICT sep_z = nullptr;
        float* HWY_RESTRICT ali_x = nullptr;
        float* HWY_RESTRICT ali_y = nullptr;
        float* HWY_RESTRICT ali_z = nullptr;
        float* HWY_RESTRICT coh_x = nullptr;
        float* HWY_RESTRICT coh_y = nullptr;
        float* HWY_RESTRICT coh_z = nullptr;
    };


    // ------------------------------------------------------------
    //  Result struct
    // ------------------------------------------------------------

    struct SteeringForces {
        Vec3 separation;
        Vec3 alignment;
        Vec3 cohesion;
    };


    // ------------------------------------------------------------
    //  Function declarations
    // ------------------------------------------------------------

    // AoS scalar path – one boid at a time
    SteeringForces compute_steering_forces_AoS(const SteeringInput& in);

    // SoA SIMD path – all boids in one call
    void compute_steering_forces_SoA(const SteeringInputSoA& in);

    // Helpers
    Vec3 combine_steering(std::initializer_list<Vec3> forces, float max_force) noexcept;
    Vec3 desired_velocity(const Vec3& current_vel, const Vec3& total_steer) noexcept;

} // namespace simnet::boid

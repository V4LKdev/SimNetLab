#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace simnet {
    /// Global simulation configuration
    /// Contains every runtime-tunable parameter.
    struct SimConfig {
        // ----------------------------------------------------
        // Simulation Timing
        // ----------------------------------------------------
        /// Ticks per second
        int sim_hz = 30;
        /// Maximum steps per frame (spiral-of-death cap)
        int max_sim_steps = 5;
        /// Maximum accumulated time before clamping (Inferred)
        double max_accum_sec = max_sim_steps / sim_hz;

        /// Returns the nominal tick duration in seconds
        float dt_seconds() const { return 1.0 / static_cast<float>(sim_hz); }
        /// Returns the nominal tick duration in nanoseconds (truncated)
        int64_t dt_ns() const { return 1'000'000'000 / sim_hz; }
        /// Maximum accumulated nanoseconds befor the accumulator is clamped
        int64_t max_accum_ns() const { return max_sim_steps * dt_ns(); }

        // ----------------------------------------------------
        // World / Environment
        // ----------------------------------------------------
        /// Half-extent of the toroidal world cube
        float world_half = 50.0f;
        /// Hard cap on the total number of boid entities at once
        int max_boids = 200.0f;
        /// Visual scale of boids
        float boid_scale = 1.0f;

        // -----------------------------------------------------
        //  Boid Dynamics
        // -----------------------------------------------------
        /// Max speed of boids
        float max_speed = 10.0f;
        /// Fraction of max speed for steering
        float max_accel_frac = 3.0f;
        /// Maximum steering acceleration / velocity for boids
        float max_accel() const { return max_speed * max_accel_frac; }

        float separation_strength = 12.0f;
        float alignment_strength = 8.0f;
        float cohesion_strength = 8.0f;

        float separation_radius = 5.0f;
        float alignment_radius = 7.5f;
        float cohesion_radius = 9.0f;

        float separation_fov_cos = -0.707f;
        float alignment_fov_cos = 0.7f;
        float cohesion_fov_cos = 0.15f;

        // ------------------------------------------------------
        // Simulation Feature Flags
        // ------------------------------------------------------
        bool enable_alignment = true;
        bool enable_cohesion = true;
        bool enable_separation = true;

        bool enable_wandering = false;
        bool enable_predator = false;
        bool enable_lure = false;

        // ------------------------------------------------------
        // Backend Selection
        // ------------------------------------------------------
        std::string simd_backend = "scalar"; // "scalar" or "simd"

        // ----------------------------------------------------
        // Deterministic Seed
        // ----------------------------------------------------
        uint64_t seed = 0;

        // ------------------------------------------------------
        // Default Factory
        // ------------------------------------------------------
        static SimConfig default_config() { return {}; }
    };
}

#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

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
        float dt_seconds() const { return 1.0f / static_cast<float>(sim_hz); }
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
        // Serialization & Identity
        // ------------------------------------------------------
        /// Serialise to a JSON object (all fields)
        [[nodiscard]]
        nlohmann::json to_json() const
        {
            nlohmann::json j;
            j["sim_hz"] = sim_hz;
            j["max_sim_steps"] = max_sim_steps;
            j["max_accum_sec"] = max_accum_sec;
            j["world_half"] = world_half;
            j["max_boids"] = max_boids;
            j["boid_scale"] = boid_scale;
            j["max_speed"] = max_speed;
            j["max_accel_frac"] = max_accel_frac;
            j["separation_strength"] = separation_strength;
            j["alignment_strength"] = alignment_strength;
            j["cohesion_strength"] = cohesion_strength;
            j["separation_radius"] = separation_radius;
            j["alignment_radius"] = alignment_radius;
            j["cohesion_radius"] = cohesion_radius;
            j["separation_fov_cos"] = separation_fov_cos;
            j["alignment_fov_cos"] = alignment_fov_cos;
            j["cohesion_fov_cos"] = cohesion_fov_cos;
            j["enable_alignment"] = enable_alignment;
            j["enable_cohesion"] = enable_cohesion;
            j["enable_separation"] = enable_separation;
            j["enable_wandering"] = enable_wandering;
            j["enable_predator"] = enable_predator;
            j["enable_lure"] = enable_lure;
            j["simd_backend"] = simd_backend;
            j["seed"] = seed;
            return j;
        }

        /// Compute a stable 64-bit hash from the full JSON representation for comparison.
        [[nodiscard]]
        uint64_t fingerprint() const
        {
            const std::string json_str = to_json().dump();

            // FNV-1a 64-bit hash
            constexpr uint64_t fnv_prime = 1099511628211ULL;
            constexpr uint64_t fnv_offset_basis = 14695981039346656037ULL;
            uint64_t hash = fnv_offset_basis;
            for (char c: json_str) {
                hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
                hash *= fnv_prime;
            }
            return hash;
        }

        // ------------------------------------------------------
        // Default Factory
        // ------------------------------------------------------
        static SimConfig default_config() { return {}; }
    };
}

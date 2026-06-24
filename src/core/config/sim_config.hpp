#pragma once

#include <cstdint>
#include <string>
#include <bit>
#include <vector>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

namespace simnet::core::config {
    static void append_u8(std::vector<uint8_t> &buf, uint8_t v) { buf.push_back(v); }

    static void append_u16(std::vector<uint8_t> &buf, uint16_t v)
    {
        buf.push_back(static_cast<uint8_t>(v >> 8));
        buf.push_back(static_cast<uint8_t>(v));
    }

    static void append_u32(std::vector<uint8_t> &buf, uint32_t v)
    {
        buf.push_back(static_cast<uint8_t>(v >> 24));
        buf.push_back(static_cast<uint8_t>(v >> 16));
        buf.push_back(static_cast<uint8_t>(v >> 8));
        buf.push_back(static_cast<uint8_t>(v));
    }

    static void append_u64(std::vector<uint8_t> &buf, uint64_t v)
    {
        buf.push_back(static_cast<uint8_t>(v >> 56));
        buf.push_back(static_cast<uint8_t>(v >> 48));
        buf.push_back(static_cast<uint8_t>(v >> 40));
        buf.push_back(static_cast<uint8_t>(v >> 32));
        buf.push_back(static_cast<uint8_t>(v >> 24));
        buf.push_back(static_cast<uint8_t>(v >> 16));
        buf.push_back(static_cast<uint8_t>(v >> 8));
        buf.push_back(static_cast<uint8_t>(v));
    }

    static void append_f32(std::vector<uint8_t> &buf, float v)
    {
        append_u32(buf, std::bit_cast<uint32_t>(v));
    }

    static void append_bool(std::vector<uint8_t> &buf, bool v)
    {
        append_u8(buf, v ? 1 : 0);
    }

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
        [[nodiscard]] double max_accum_sec() const { return static_cast<double>(max_sim_steps) / sim_hz; }

        /// Returns the nominal tick duration in seconds
        [[nodiscard]] float dt_seconds() const { return 1.0f / static_cast<float>(sim_hz); }
        /// Returns the nominal tick duration in nanoseconds (truncated)
        [[nodiscard]] int64_t dt_ns() const { return 1'000'000'000 / sim_hz; }
        /// Maximum accumulated nanoseconds befor the accumulator is clamped
        [[nodiscard]] int64_t max_accum_ns() const { return max_sim_steps * dt_ns(); }

        // ----------------------------------------------------
        // World / Environment
        // ----------------------------------------------------
        /// Half-extent of the toroidal world cube
        float world_half = 50.0f;
        /// Hard cap on the total number of boid entities at once
        int max_boids = 200;
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
        [[nodiscard]] float max_accel() const { return max_speed * max_accel_frac; }

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
        bool simd_backend = false; // "scalar" or "simd"

        // ------------------------------------------------------
        // Deterministic Seed
        // ------------------------------------------------------
        uint64_t seed = 0;

        // ------------------------------------------------------
        // Multithreading
        // ------------------------------------------------------
        bool multithreaded_ecs = true;
        int ecs_thread_count = 4; // 0 = auto

        // ------------------------------------------------------
        // Network
        // ------------------------------------------------------
        int snapshot_send_interval = 1; // send every N simulation ticks
        uint16_t net_port = 7777;
        size_t net_max_peers = 10; // server only

        // ------------------------------------------------------
        // Pipeline Technique Flags
        // ------------------------------------------------------
        bool enable_send_interval = false;
        bool enable_incremental_snapshots = false;
        bool enable_delta_compression = false;
        bool enable_variable_bit_delta_indexing = false;
        bool enable_float_quantization = false;
        bool enable_octahedral_heading = false;
        bool enable_bit_packing = false;
        bool enable_aoi_filter = false;
        bool enable_vector_field_consistency = false;
        bool enable_entity_lod = false;
        bool enable_leader_follower = false;
        bool enable_full_snapshot_compression = false;
        bool enable_huffman_zstd_dictionary_compression = false;
        bool enable_dead_reckoning = false;
        bool enable_dirty_tracking = false;
        bool enable_tcm = false;


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
            j["multithreaded_ecs"] = multithreaded_ecs;
            j["ecs_thread_count"] = ecs_thread_count;
            j["snapshot_send_interval"] = snapshot_send_interval;
            j["net_port"] = net_port;
            j["net_max_peers"] = net_max_peers;
            j["enable_send_interval"] = enable_send_interval;
            j["enable_incremental_snapshots"] = enable_incremental_snapshots;
            j["enable_delta_compression"] = enable_delta_compression;
            j["enable_variable_bit_delta_indexing"] = enable_variable_bit_delta_indexing;
            j["enable_float_quantization"] = enable_float_quantization;
            j["enable_octahedral_heading"] = enable_octahedral_heading;
            j["enable_bit_packing"] = enable_bit_packing;
            j["enable_aoi_filter"] = enable_aoi_filter;
            j["enable_vector_field_consistency"] = enable_vector_field_consistency;
            j["enable_entity_lod"] = enable_entity_lod;
            j["enable_leader_follower"] = enable_leader_follower;
            j["enable_full_snapshot_compression"] = enable_full_snapshot_compression;
            j["enable_huffman_zstd_dictionary_compression"] = enable_huffman_zstd_dictionary_compression;
            j["enable_dead_reckoning"] = enable_dead_reckoning;
            j["enable_dirty_tracking"] = enable_dirty_tracking;
            j["enable_tcm"] = enable_tcm;
            return j;
        }

        /// Compute a stable 64-bit hash from the full JSON representation for comparison.
        [[nodiscard]]
        uint64_t fingerprint() const
        {
            std::vector<uint8_t> bytes;
            bytes.reserve(128); // approximate size

            // --- Ints ---
            append_u32(bytes, static_cast<uint32_t>(sim_hz));
            append_u32(bytes, static_cast<uint32_t>(max_sim_steps));
            append_u32(bytes, static_cast<uint32_t>(max_boids));
            append_u32(bytes, static_cast<uint32_t>(snapshot_send_interval));
            append_u32(bytes, static_cast<uint32_t>(net_port));
            append_u64(bytes, static_cast<uint64_t>(net_max_peers));
            append_u64(bytes, seed);
            append_u32(bytes, static_cast<uint32_t>(ecs_thread_count));

            // --- Floats ---
            append_f32(bytes, world_half);
            append_f32(bytes, boid_scale);
            append_f32(bytes, max_speed);
            append_f32(bytes, max_accel_frac);
            append_f32(bytes, separation_strength);
            append_f32(bytes, alignment_strength);
            append_f32(bytes, cohesion_strength);
            append_f32(bytes, separation_radius);
            append_f32(bytes, alignment_radius);
            append_f32(bytes, cohesion_radius);
            append_f32(bytes, separation_fov_cos);
            append_f32(bytes, alignment_fov_cos);
            append_f32(bytes, cohesion_fov_cos);

            // --- Bools (order matters!) ---
            append_bool(bytes, enable_alignment);
            append_bool(bytes, enable_cohesion);
            append_bool(bytes, enable_separation);
            append_bool(bytes, enable_wandering);
            append_bool(bytes, enable_predator);
            append_bool(bytes, enable_lure);
            append_bool(bytes, simd_backend);
            append_bool(bytes, multithreaded_ecs);
            append_bool(bytes, enable_send_interval);
            append_bool(bytes, enable_incremental_snapshots);
            append_bool(bytes, enable_delta_compression);
            append_bool(bytes, enable_variable_bit_delta_indexing);
            append_bool(bytes, enable_float_quantization);
            append_bool(bytes, enable_octahedral_heading);
            append_bool(bytes, enable_bit_packing);
            append_bool(bytes, enable_aoi_filter);
            append_bool(bytes, enable_vector_field_consistency);
            append_bool(bytes, enable_entity_lod);
            append_bool(bytes, enable_leader_follower);
            append_bool(bytes, enable_full_snapshot_compression);
            append_bool(bytes, enable_huffman_zstd_dictionary_compression);
            append_bool(bytes, enable_dead_reckoning);
            append_bool(bytes, enable_dirty_tracking);
            append_bool(bytes, enable_tcm);

            // --- Hash the binary blob ---
            constexpr uint64_t FNV_PRIME = 1099511628211ULL;
            constexpr uint64_t OFFSET_BASIS = 14695981039346656037ULL;
            uint64_t hash = OFFSET_BASIS;
            for (uint8_t c: bytes) {
                hash ^= static_cast<uint64_t>(c);
                hash *= FNV_PRIME;
            }
            return hash;
        }

        // ------------------------------------------------------
        // Default Factory
        // ------------------------------------------------------
        static SimConfig default_config() { return {}; }
    };
}

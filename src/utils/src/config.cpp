/// \file   config.cpp
/// \brief  Deterministic hash computation for SimConfig.

module;

#include <cstdint>
#include <string>
#include <vector>
#include <bit>

module simnet.utils;

namespace simnet::utils {
    namespace {
        void append_u8(std::vector<uint8_t> &buf, uint8_t v) { buf.push_back(v); }

        void append_u32(std::vector<uint8_t> &buf, uint32_t v)
        {
            buf.push_back(static_cast<uint8_t>(v >> 24));
            buf.push_back(static_cast<uint8_t>(v >> 16));
            buf.push_back(static_cast<uint8_t>(v >> 8));
            buf.push_back(static_cast<uint8_t>(v));
        }

        void append_u64(std::vector<uint8_t> &buf, uint64_t v)
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

        void append_f32(std::vector<uint8_t> &buf, float v)
        {
            append_u32(buf, std::bit_cast<uint32_t>(v));
        }

        void append_bool(std::vector<uint8_t> &buf, bool v)
        {
            append_u8(buf, v ? 1 : 0);
        }

        void append_string(std::vector<uint8_t> &buf, const std::string &v)
        {
            append_u32(buf, static_cast<uint32_t>(v.size()));
            for (const char c: v) {
                append_u8(buf, static_cast<uint8_t>(c));
            }
        }
    }

    uint64_t fingerprint(const SimConfig &cfg)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(128);

        // Config fingerprint contract: update this stable field order whenever SimConfig's parsed, validated JSON contract changes.

        append_u32(bytes, static_cast<uint32_t>(cfg.sim_hz));
        append_u32(bytes, static_cast<uint32_t>(cfg.max_sim_steps));
        append_u32(bytes, static_cast<uint32_t>(cfg.max_boids));
        append_u32(bytes, cfg.target_exact_neighbors);
        append_u32(bytes, cfg.aggregate_weight_cap);
        append_u64(bytes, cfg.seed);
        append_u32(bytes, static_cast<uint32_t>(cfg.ecs_thread_count));
        append_u32(bytes, static_cast<uint32_t>(cfg.statistics_interval_ticks));

        append_f32(bytes, cfg.world_half);
        append_f32(bytes, cfg.body_radius);
        append_f32(bytes, cfg.max_speed);
        append_f32(bytes, cfg.max_accel_frac);
        append_f32(bytes, cfg.separation_strength);
        append_f32(bytes, cfg.local_alignment_strength);
        append_f32(bytes, cfg.aggregate_alignment_strength);
        append_f32(bytes, cfg.aggregate_cohesion_strength);
        append_f32(bytes, cfg.separation_radius);
        append_f32(bytes, cfg.local_alignment_radius);
        append_f32(bytes, cfg.cohesion_radius);
        append_f32(bytes, cfg.hue_radius);
        append_f32(bytes, cfg.exact_cell_size);
        append_f32(bytes, cfg.aggregate_cell_size);
        append_f32(bytes, cfg.fov_cos);
        append_f32(bytes, cfg.min_distance);
        append_f32(bytes, cfg.hue_adaptation_rate);

        append_bool(bytes, cfg.enable_alignment);
        append_bool(bytes, cfg.enable_cohesion);
        append_bool(bytes, cfg.enable_separation);
        append_bool(bytes, cfg.enable_hue_adaptation);
        append_bool(bytes, cfg.multithreaded_ecs);
        append_bool(bytes, cfg.enable_aoi);
        append_bool(bytes, cfg.enable_delta);
        append_bool(bytes, cfg.enable_quantization);
        append_bool(bytes, cfg.enable_compression);
        append_string(bytes, cfg.log_level);

        // FNV-1a fingerprint
        constexpr uint64_t FNV_PRIME = 1099511628211ULL;
        constexpr uint64_t OFFSET_BASIS = 14695981039346656037ULL;
        uint64_t hash = OFFSET_BASIS;
        for (uint8_t c: bytes) {
            hash ^= static_cast<uint64_t>(c);
            hash *= FNV_PRIME;
        }
        return hash;
    }
}

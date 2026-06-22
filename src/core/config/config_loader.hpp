#pragma once

#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "sim_config.hpp"
#include "telemetry/telemetry.hpp"

namespace simnet::core::config {
    inline bool load_json(const std::string &path, SimConfig &cfg)
    {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        try {
            nlohmann::json j;
            in >> j;

            // use value() with existing cfg as default source.
            cfg.sim_hz = j.value("sim_hz", cfg.sim_hz);
            cfg.max_sim_steps = j.value("max_sim_steps", cfg.max_sim_steps);
            cfg.max_accum_sec = j.value("max_accum_sec", cfg.max_accum_sec);
            cfg.world_half = j.value("world_half", cfg.world_half);
            cfg.max_boids = j.value("max_boids", cfg.max_boids);
            cfg.boid_scale = j.value("boid_scale", cfg.boid_scale);
            cfg.max_speed = j.value("max_speed", cfg.max_speed);
            cfg.max_accel_frac = j.value("max_accel_frac", cfg.max_accel_frac);
            cfg.separation_strength = j.value("separation_strength", cfg.separation_strength);
            cfg.alignment_strength = j.value("alignment_strength", cfg.alignment_strength);
            cfg.cohesion_strength = j.value("cohesion_strength", cfg.cohesion_strength);
            cfg.separation_radius = j.value("separation_radius", cfg.separation_radius);
            cfg.alignment_radius = j.value("alignment_radius", cfg.alignment_radius);
            cfg.cohesion_radius = j.value("cohesion_radius", cfg.cohesion_radius);
            cfg.separation_fov_cos = j.value("separation_fov_cos", cfg.separation_fov_cos);
            cfg.alignment_fov_cos = j.value("alignment_fov_cos", cfg.alignment_fov_cos);
            cfg.cohesion_fov_cos = j.value("cohesion_fov_cos", cfg.cohesion_fov_cos);
            cfg.enable_alignment = j.value("enable_alignment", cfg.enable_alignment);
            cfg.enable_cohesion = j.value("enable_cohesion", cfg.enable_cohesion);
            cfg.enable_separation = j.value("enable_separation", cfg.enable_separation);
            cfg.enable_wandering = j.value("enable_wandering", cfg.enable_wandering);
            cfg.enable_predator = j.value("enable_predator", cfg.enable_predator);
            cfg.enable_lure = j.value("enable_lure", cfg.enable_lure);

            if (j.contains("simd_backend")) {
                cfg.simd_backend = j["simd_backend"].get<std::string>();
            }
            cfg.seed = j.value("seed", cfg.seed);
            cfg.multithreaded_ecs = j.value("multithreaded_ecs", cfg.multithreaded_ecs);
            cfg.ecs_thread_count = j.value("ecs_thread_count", cfg.ecs_thread_count);

            return true;
        } catch (std::exception &e) {
            TELEM_LOG_ERROR("Failed to parse config JSON: {}", e.what());
            return false;
        }
    }

    inline void save_json(const std::string &path, const SimConfig &cfg)
    {
        nlohmann::json j = cfg.to_json();
        std::ofstream out(path);
        out << std::setw(2) << j << std::endl;
    }
}

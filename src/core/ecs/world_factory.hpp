#pragma once

#include <flecs.h>
#include <thread>

#include "core/config/sim_config.hpp"
#include "telemetry.hpp"

namespace simnet::core::ecs {
    /// Factory for setting up and configuring flecs worlds.
    struct WorldFactory {
        /**
         * Configure the world's thread count according to the simulation config.
         */
        static void configure_threads(flecs::world &world,
                                      const config::SimConfig &cfg)
        {
            if (!cfg.multithreaded_ecs) {
                world.set_threads(0);
                return;
            }

            if (cfg.ecs_thread_count > 0) {
                world.set_threads(cfg.ecs_thread_count);
                return;
            }

            unsigned int num_threads = std::thread::hardware_concurrency();
            if (num_threads == 0) {
                num_threads = 4;
                TELEM_LOG_WARN("Could not determine hardware concurrency; defaulting to 4 threads");
            }

            // Leave one core free for better game‑loop latency
            if (num_threads > 4) {
                num_threads -= 1;
            }

            world.set_threads(static_cast<int32_t>(num_threads));
        }

        // Future expansion:
        // static flecs::world create_headless(const SimConfig& cfg);
        // static void register_core_modules(flecs::world& world);
    };
}

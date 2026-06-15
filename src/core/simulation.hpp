#pragma once
#include <flecs.h>

#include "SimConfig.hpp"
#include "ecs/components.hpp"

namespace simnet::sim {
    /*
     *  Glue layer: owns ecs world lifecycle, orchestrates per-tick dispatch,
     *  and might later contain network related orchestration? (or I use systems)
     */
    class Simulation {
    public:
        Simulation(const SimConfig &cfg);

        ~Simulation();

        Simulation(const Simulation &) = delete;

        Simulation &operator=(const Simulation &) = delete;

        Simulation(Simulation &&) = delete;

        Simulation &operator=(Simulation &&) = delete;

        /// Run one simulation tick.
        void step();

        /// Monotonically increasing tick counter. (Should this be on the controller?)
        [[nodiscard]]
        uint64_t current_tick() const;

        /// Read-only access to the underlying flecs world.
        /// TODO: move to proper spawn system / factory.
        [[nodiscard]]
        const flecs::world &world() const { return world_; }

    private:
        void spawn_boids(uint32_t count);

        flecs::world world_;
        uint64_t tick_ = 0;
        const SimConfig *cfg_ = nullptr;
    };
}

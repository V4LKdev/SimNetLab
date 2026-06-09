#pragma once
#include <flecs.h>

namespace simnet::sim {
    class Simulation {
    public:
        Simulation();

        void step();

        [[nodiscard]]
        uint64_t current_tick() const;

        [[nodiscard]]
        const flecs::world &world() const { return world_; }

    private:
        void spawn_boids(uint32_t count);

        void init_singletons();

        flecs::world world_;
        uint64_t tick_ = 0;
    };
}

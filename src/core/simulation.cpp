#include "simulation.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "components.hpp"
#include "config.hpp"
#include "singletons.hpp"
#include "systems.hpp"

namespace
{
    float random_float(float min, float max)
    {
        thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_real_distribution<float> dis(min, max);
        return dis(gen);
    }
}

namespace simnet::sim {

    Simulation::Simulation()
    {

        // Components
        world_.component<ecs::Position3D>();
        world_.component<ecs::Velocity3D>();
        world_.component<ecs::DesiredVelocity3D>();

        // Tags
        world_.component<ecs::Boid>();


        ecs::init_singletons(world_);

        ecs::init_systems(world_);

        spawn_boids(config::MAX_BOIDS);
    }

    void Simulation::step()
    {
        world_.progress(config::SIM_DT_SECONDS);

        ++tick_;
    }

    uint64_t Simulation::current_tick() const
    {
        return tick_;
    }

    void Simulation::spawn_boids(uint32_t count)
    {
        const float half = config::WORLD_HALF;

        for (uint32_t i = 0; i < count; ++i) {
            auto e = world_.entity();

            e.set<ecs::Position3D>({
                random_float(-half, half),
                random_float(-half, half),
                random_float(-half, half),
            });

            e.set<ecs::Velocity3D>({
            random_float(-half, half),
            random_float(-half, half),
            random_float(-half, half),
            });

            e.set<ecs::DesiredVelocity3D>({0.0f, 0.0f, 0.0f});

            e.add<ecs::Boid>();
        }
    }

}

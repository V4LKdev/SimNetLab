#include "simulation.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "ecs/components.hpp"
#include "config.hpp"
#include "ecs/systems.hpp"

namespace {
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
        world_.component<ecs::Position>();
        world_.component<ecs::Velocity>();
        world_.component<ecs::DesiredVelocity>();

        // Tags
        world_.component<ecs::Boid>();

        init_singletons();
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

            e.set<ecs::Position>({
                Vec3(random_float(-half, half),
                     random_float(-half, half),
                     random_float(-half, half))
            });

            e.set<ecs::Velocity>({
                Vec3(random_float(-half, half),
                     random_float(-half, half),
                     random_float(-half, half))
            });

            e.set<ecs::DesiredVelocity>({
                Vec3(0.0f, 0.0f, 0.0f)
            });

            e.add<ecs::Boid>();
        }
    }

    void Simulation::init_singletons()
    {
        world_.component<ecs::Boid>();
        world_.component<ecs::Position>();
        world_.component<ecs::Velocity>();
        world_.component<ecs::DesiredVelocity>();

        world_.component<ecs::BoidConfig>();
        world_.component<ecs::BoidPerception>();

        world_.set<ecs::BoidConfig>({});
        world_.set<ecs::BoidPerception>({});
    }
}

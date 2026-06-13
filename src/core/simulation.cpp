#include "simulation.hpp"

#include <random>

#include "ecs/components.hpp"
#include "config.hpp"
#include "telemetry.hpp"
#include "ecs/pipeline.hpp"

namespace {
    constexpr bool USE_DEBUG_BOIDS = false;

    float random_float(const float min, const float max)
    {
        thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_real_distribution<float> dis(min, max);
        return dis(gen);
    }
}

namespace simnet::sim {
    Simulation::Simulation()
    {
        world_.set_threads(0);

        ecs::init_simulation(world_);
        spawn_boids(config::MAX_BOIDS);
    }

    Simulation::~Simulation()
    {
        world_.set_threads(0);
        world_.quit();
    }

    void Simulation::step()
    {
        TELEM_TRACY_ZONE_C("SimulationFrame", TELEM_COLOR_SIM);
        ecs::run_tick(world_, config::SIM_DT_SECONDS);
        ++tick_;

        if (tick_ % 100 == 0) {
            TELEM_LOG_DEBUG("Sim tick {} complete", tick_);
        }

        TELEM_TRACY_PLOT("SimTick", static_cast<int64_t>(tick_));
        TELEM_TRACY_FRAME("SimStep");
    }

    uint64_t Simulation::current_tick() const
    {
        return tick_;
    }

    void Simulation::spawn_boids(const uint32_t count) const
    {
        if constexpr (USE_DEBUG_BOIDS) {
            constexpr uint32_t TEST_COUNT = 2;
            constexpr float distance = config::WORLD_HALF / 2.f;
            constexpr float max_speed = 10.f;

            Vec3 positions[TEST_COUNT] = {
                Vec3(distance, 0.0f, 0.0f), // +X
                Vec3(0.0f, 0.0f, distance) // +Z
            };

            for (auto pos : positions) {
                auto e = world_.entity();
                Vec3 dir = Vec3(0.0f, 0.0f, 0.0f) - pos;
                Vec3 vel = dir.normalized() * max_speed;

                e.set<ecs::Position>({pos});
                e.set<ecs::Velocity>({vel});
                e.set<ecs::SteeringAccumulate>({Vec3(0.0f, 0.0f, 0.0f)});
                e.set<ecs::Heading>({vel.normalized()});
                e.set<ecs::NeighborList>({});
                e.add<ecs::Boid>();
            }
        } else {
            constexpr float half = config::WORLD_HALF;
            constexpr float max_speed = 50.f;

            for (uint32_t i = 0; i < count; ++i) {
                auto e = world_.entity();

                const Vec3 pos(random_float(-half, half),
                         random_float(-half, half),
                         random_float(-half, half));

                Vec3 dir(random_float(-1.f, 1.f),
                         random_float(-1.f, 1.f),
                         random_float(-1.f, 1.f));
                Vec3 vel = dir.normalized() * max_speed;

                e.set<ecs::Position>({pos});
                e.set<ecs::Velocity>({vel});
                e.set<ecs::SteeringAccumulate>({Vec3(0.0f, 0.0f, 0.0f)});
                e.set<ecs::Heading>({vel.normalized()});
                e.set<ecs::NeighborList>({});
                e.add<ecs::Boid>();
            }
        }
    }
}

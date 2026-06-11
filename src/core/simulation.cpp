#include "simulation.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "ecs/components.hpp"
#include "config.hpp"
#include "telemetry.hpp"
#include "../../OldTemp/systems.hpp"
#include "ecs/pipeline.hpp"

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
        world_.set_threads(4);

        ecs::init_simulation(world_);

        spawn_boids(config::MAX_BOIDS);

#ifdef TELEMETRY_ENABLED
        telemetry::init("SimNetLab", "log/simnet_telemetry.log");
        TELEM_LOG_INFO("Telemetry initialized successfully");
#endif
    }

    Simulation::~Simulation()
    {
#ifdef TELEMETRY_ENABLED
        telemetry::shutdown();
#endif

        world_.quit();
        world_.set_threads(0);
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

        TELEM_TRACY_FRAME("SimFrame");
    }


    uint64_t Simulation::current_tick() const
    {
        return tick_;
    }

#define USE_DEBUG_BOIDS 0

    void Simulation::spawn_boids(uint32_t count)
    {
#if USE_DEBUG_BOIDS
        const uint32_t TEST_COUNT = 5;
        const float distance = config::WORLD_HALF - 20.f;
        const float max_speed = 1.f;

        Vec3 positions[TEST_COUNT] = {
            Vec3(distance, 0.0f, 0.0f), // +X Face center (Right)
            Vec3(-distance, 0.0f, 0.0f), // -X Face center (Left)
            Vec3(0.0f, distance, 0.0f), // +Y Face center (Top)
            Vec3(0.0f, 0.0f, distance), // +Z Face center (Front)
            Vec3(0.0f, 0.0f, -distance) // -Z Face center (Back)
        };

        for (uint32_t i = 0; i < TEST_COUNT; ++i) {
            auto e = world_.entity();
            Vec3 pos = positions[i];
            Vec3 dir = Vec3(0.0f, 0.0f, 0.0f) - pos;
            Vec3 vel = dir.normalized() * max_speed;

            e.set<ecs::Position>({pos});
            e.set<ecs::Velocity>({vel});
            e.set<ecs::SteeringAccumulate>({Vec3(0.0f, 0.0f, 0.0f)});
            e.set<ecs::Heading>({});
            e.set<ecs::NeighborList>({});
            e.add<ecs::Boid>();
        }
#else
        // Random spawn using the provided 'count'
        const float half = config::WORLD_HALF;
        const float max_speed = 50.f;

        for (uint32_t i = 0; i < count; ++i) {
            auto e = world_.entity();

            // Random position inside [-half, half] on all axes
            Vec3 pos(
                random_float(-half, half),
                random_float(-half, half),
                random_float(-half, half)
            );

            // Random velocity direction, scaled to max_speed
            Vec3 dir(
                random_float(-1.f, 1.f),
                random_float(-1.f, 1.f),
                random_float(-1.f, 1.f)
            );
            Vec3 vel = dir.normalized() * max_speed;

            e.set<ecs::Position>({pos});
            e.set<ecs::Velocity>({vel});
            e.set<ecs::SteeringAccumulate>({Vec3(0.0f, 0.0f, 0.0f)});
            e.set<ecs::Heading>({});
            e.set<ecs::NeighborList>({});
            e.add<ecs::Boid>();
        }
#endif
    }
}

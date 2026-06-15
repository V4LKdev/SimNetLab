#include "simulation.hpp"

#include <random>

#include "ecs/components.hpp"
#include "telemetry.hpp"
#include "ecs/pipeline.hpp"

namespace {
    constexpr bool USE_DEBUG_BOIDS = false;

    float random_float(const float min, const float max, std::mt19937 &gen)
    {
        std::uniform_real_distribution<float> dis(min, max);
        return dis(gen);
    }
}

namespace simnet::sim {
    Simulation::Simulation(const SimConfig &cfg)
    {
        world_.set_threads(4);

        ecs::init_simulation(world_);
        world_.set<SimConfig>(cfg);

        cfg_ = &world_.get<SimConfig>();

        spawn_boids(cfg_->max_boids);
    }

    Simulation::~Simulation()
    {
        world_.set_threads(0);
        world_.quit();
    }

    void Simulation::step()
    {
        TELEM_TRACY_ZONE_C("SimulationFrame", TELEM_COLOR_SIM);
        ecs::run_tick(world_, cfg_->dt_seconds());
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

    void Simulation::spawn_boids(const uint32_t count)
    {
        if constexpr (USE_DEBUG_BOIDS) {
            constexpr uint32_t TEST_COUNT = 2;
            const float distance = cfg_->world_half / 2.f;
            const float max_speed = cfg_->max_speed;

            Vec3 positions[TEST_COUNT] = {
                Vec3(distance, 0.0f, 0.0f), // +X
                Vec3(0.0f, 0.0f, distance) // +Z
            };

            for (auto pos: positions) {
                auto e = world_.entity();
                Vec3 dir = Vec3(0.0f, 0.0f, 0.0f) - pos;
                Vec3 vel = dir.normalized() * max_speed;

                e.set<ecs::Position>({pos});
                e.set<ecs::Velocity>({vel});
                e.set<ecs::SteeringAccumulate>({Vec3::zero()});
                e.set<ecs::Heading>({vel.normalized()});
                e.set<ecs::BoidIdx>({});
                e.add<ecs::Boid>();
            }
        } else {
            const float half = cfg_->world_half;
            const float max_speed = cfg_->max_speed;
            std::mt19937 gen(cfg_->seed != 0 ? cfg_->seed : std::random_device{}());
            std::uniform_int_distribution<uint8_t> dist(0, 255);

            for (uint32_t i = 0; i < count; ++i) {
                auto e = world_.entity();
                const Vec3 pos(random_float(-half, half, gen),
                               random_float(-half, half, gen),
                               random_float(-half, half, gen));
                Vec3 dir(random_float(-1.f, 1.f, gen),
                         random_float(-1.f, 1.f, gen),
                         random_float(-1.f, 1.f, gen));
                Vec3 vel = dir.normalized() * max_speed;
                e.set<ecs::Position>({pos});
                e.set<ecs::Velocity>({vel});
                e.set<ecs::Hue>({ dist(gen) });
                    e.set<ecs::SteeringAccumulate>({Vec3::zero()});
                    e.set<ecs::Heading>({vel.normalized()});
                    e.set<ecs::BoidIdx>({});
                    e.add<ecs::Boid>();

                }
            }
        }
    }

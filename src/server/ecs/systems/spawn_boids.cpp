#include "system_functions.hpp"

#include <random>

#include "telemetry.hpp"
#include "config/sim_config.hpp"
#include "../../../game/shared/components.hpp"

namespace {
    float random_float(float min, float max, std::mt19937 &gen)
    {
        std::uniform_real_distribution<float> dis(min, max);
        return dis(gen);
    }
}

namespace simnet::server::ecs {
    void spawn_boids_system(flecs::iter &it)
    {
        const auto cfg = it.world().get<SimConfig>();

        const float half = cfg.world_half;
        const float max_speed = cfg.max_speed;
        const uint32_t count = static_cast<uint32_t>(cfg.max_boids);

        std::mt19937 gen(cfg.seed != 0 ? cfg.seed : std::random_device{}());
        std::uniform_int_distribution<uint8_t> hue_dist(0, 255);

        while (it.next()) {
            for (uint32_t i = 0; i < count; ++i) {
                auto e = it.world().entity();

                // Random position inside the world cube
                const Vec3 pos(random_float(-half, half, gen),
                               random_float(-half, half, gen),
                               random_float(-half, half, gen));

                // Random direction → velocity
                Vec3 dir(random_float(-1.f, 1.f, gen),
                         random_float(-1.f, 1.f, gen),
                         random_float(-1.f, 1.f, gen));
                Vec3 vel = dir.normalized() * max_speed;

                e.set<simnet::ecs::Position>({pos});
                e.set<simnet::ecs::Velocity>({vel});
                e.set<simnet::ecs::Heading>({vel.normalized()});
                e.set<simnet::ecs::Hue>({hue_dist(gen)});
                e.set<simnet::ecs::SteeringAccumulate>({Vec3::zero()});
                e.set<simnet::ecs::BoidIdx>({});
                e.set<simnet::ecs::NetworkId>({i});
                e.add<simnet::ecs::Boid>();
            }
        }

        TELEM_LOG_INFO("Spawning boids");
    }
}

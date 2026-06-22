#include <random>

#include "system_functions.hpp"

#include "telemetry/telemetry.hpp"
#include "game/shared/game_shared.hpp"

namespace simnet::game::server {
    namespace {
        // --- Helpers ---
        math::Vec3 random_direction(std::mt19937 &rng)
        {
            // Uniformly distributed direction on unit sphere
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            float theta = 2.0f * std::numbers::pi_v<float> * dist(rng);
            float phi = std::acos(2.0f * dist(rng) - 1.0f);

            return math::Vec3{
                std::sin(phi) * std::cos(theta),
                std::sin(phi) * std::sin(theta),
                std::cos(phi)
            };
        }

        float random_float(float min, float max, std::mt19937 &rng)
        {
            std::uniform_real_distribution<float> dist(min, max);
            return dist(rng);
        }

        void spawn_single_boid(flecs::world &world, const config::SimConfig &config, uint32_t network_id,
                               std::mt19937 &rng)
        {
            using namespace shared;

            // Spawn position within a sphere of radius world_half * 0.7
            float radius = config.world_half * 0.7f;
            Vec3 position = random_direction(rng) * (random_float(0.0f, radius, rng));
            Vec3 heading = random_direction(rng);

            // TODO: non-random assignment of hue
            uint8_t hue = random_float(0.0f, 255.0f, rng);

            auto e = world.entity()
                    .set<Position>({position})
                    .set<Velocity>({Vec3::zero()})
                    .set<Heading>({heading})
                    .set<Hue>({hue})
                    .set<NetworkId>({network_id})
                    .set<BoidIdx>({0}) // gets written per frame
                    .set<SteeringAccumulate>({Vec3::zero()})
                    .add<Boid>(); // Tag

            TELEM_LOG_TRACE("Spawned boid net_id={}, e={}", network_id, e.id());
        }

        void destroy_oldest_boids(flecs::world &world, size_t to_destroy)
        {
            if (to_destroy == 0) return;

            size_t destroyed = 0;

            // Flecs iteration order reflects creation order.
            auto q = world.query_builder<const shared::NetworkId>().build();

            q.each([&](flecs::entity e, const shared::NetworkId &) {
                if (destroyed >= to_destroy) return;
                e.destruct();
                ++destroyed;
            });

            if (destroyed < to_destroy) {
                TELEM_LOG_WARN("Only {} of {} boids destroyed", destroyed, to_destroy);
            }
        }
    }

    void boid_population_manager_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_BoidPopulationManager");

        auto world = it.world();
        const config::SimConfig &config = world.get<config::SimConfig>();

        // 1. Count current boids
        size_t current_count = world.query<shared::Boid>().count();

        // 2. reset network ID generator if 0 boids
        if (current_count == 0 && config.max_boids > 0) {
            utils::reset_network_id(1); // start from 1
        }

        // 3. If at target, nothing to do
        if (current_count == static_cast<size_t>(config.max_boids)) {
            return;
        }

        // 4. Create deterministic rng
        uint32_t next_id = utils::get_current_id();
        uint64_t seed_offset = next_id;
        std::mt19937 rng(config.seed + seed_offset);

        // 5. Spawn missing boids
        if (current_count < static_cast<size_t>(config.max_boids)) {
            size_t to_spawn = static_cast<size_t>(config.max_boids) - current_count;

            for (size_t i = 0; i < to_spawn; i++) {
                uint32_t net_id = utils::generate_network_id();
                spawn_single_boid(world, config, net_id, rng);
            }
            TELEM_LOG_DEBUG("Spawned {} boids", to_spawn);
        }

        // 6. Destroy excess boids
        if (current_count > static_cast<size_t>(config.max_boids)) {
            size_t to_destroy = current_count - static_cast<size_t>(config.max_boids);
            destroy_oldest_boids(world, to_destroy);
            TELEM_LOG_DEBUG("Destroyed {} boids", to_destroy);
        }
    }
}

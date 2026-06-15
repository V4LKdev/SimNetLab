#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include <flecs.h>

#include "../src/core/config/SimConfig.hpp"
#include "ecs/components.hpp"
#include "ecs/pipeline.hpp"
#include "math/vec3.hpp"

using simnet::Vec3;
using Catch::Approx;

namespace {
    bool vec3_approx(const Vec3 &a, const Vec3 &b, float eps = 1e-4f)
    {
        return std::abs(a.x() - b.x()) <= eps &&
               std::abs(a.y() - b.y()) <= eps &&
               std::abs(a.z() - b.z()) <= eps;
    }
}

TEST_CASE("Integration: two boids separate under strong repulsion", "[integration]")
{
    flecs::world world;
    simnet::ecs::init_simulation(world);

    simnet::SimConfig cfg = simnet::SimConfig::default_config();
    cfg.max_speed = 10.0f;
    cfg.max_accel_frac = 3.0f;
    cfg.separation_strength = 100.0f;
    cfg.alignment_strength = 0.0f;
    cfg.cohesion_strength = 0.0f;
    cfg.separation_radius = 5.0f;
    cfg.separation_fov_cos = -1.0f;
    world.set<simnet::SimConfig>(cfg);

    const float dt = cfg.dt_seconds();

    auto e_a = world.entity()
            .set<simnet::ecs::Position>({Vec3{0.0f, 0.0f, 0.0f}})
            .set<simnet::ecs::Velocity>({Vec3{0.0f, 0.0f, 0.0f}})
            .set<simnet::ecs::Heading>({Vec3{1.0f, 0.0f, 0.0f}})
            .set<simnet::ecs::SteeringAccumulate>({Vec3::zero()})
            .set<simnet::ecs::BoidIdx>({0})
            .add<simnet::ecs::Boid>();

    auto e_b = world.entity()
            .set<simnet::ecs::Position>({Vec3{1.0f, 0.0f, 0.0f}})
            .set<simnet::ecs::Velocity>({Vec3{0.0f, 0.0f, 0.0f}})
            .set<simnet::ecs::Heading>({Vec3{-1.0f, 0.0f, 0.0f}})
            .set<simnet::ecs::SteeringAccumulate>({Vec3::zero()})
            .set<simnet::ecs::BoidIdx>({1})
            .add<simnet::ecs::Boid>();

    simnet::ecs::run_tick(world, dt);

    const simnet::ecs::Position final_pos_a = e_a.get<simnet::ecs::Position>();
    const simnet::ecs::Velocity final_vel_a = e_a.get<simnet::ecs::Velocity>();
    const simnet::ecs::Position final_pos_b = e_b.get<simnet::ecs::Position>();
    const simnet::ecs::Velocity final_vel_b = e_b.get<simnet::ecs::Velocity>();

    world.quit();

    // Velocities after one tick: ±1 m/s
    REQUIRE(vec3_approx(final_vel_a.value, Vec3{-1.0f, 0.0f, 0.0f}));
    REQUIRE(vec3_approx(final_vel_b.value, Vec3{ 1.0f, 0.0f, 0.0f}));

    // Positions: 0 + (-1)*(1/30) = -0.03333…  and  1 + (1)*(1/30) = 1.03333…
    REQUIRE(vec3_approx(final_pos_a.value, Vec3{-1.0f / 30.0f, 0.0f, 0.0f}, 1e-4f));
    REQUIRE(vec3_approx(final_pos_b.value, Vec3{ 1.0f + 1.0f / 30.0f, 0.0f, 0.0f}, 1e-4f));
}

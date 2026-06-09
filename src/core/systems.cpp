#include "systems.hpp"

#include <cmath>

#include "components.hpp"
#include "config.hpp"
#include "boid_steering.hpp"

namespace simnet::ecs {

    void init_systems(flecs::world& world)
    {
        register_scratchpad_builder(world);
        register_boid_steering_system(world);
        register_boid_apply_velocity(world);
        register_boid_integrate_position(world);
    }

    // ------------------------------------------------------------------
    //  Scratchpad / Neighbour list builder (PreUpdate)
    // ------------------------------------------------------------------
    void register_scratchpad_builder(const flecs::world &world)
    {
        world.system<const Position3D, const Velocity3D, Boid>("BuildScratchpad")
            .kind(flecs::PreUpdate)
            .run([](flecs::iter& it) {

                auto& neighbours = it.world().get_mut<NeighbourList>();
                auto& soa        = it.world().get_mut<BoidScratchpadSoA>();

                const size_t numBoids = it.world().count<Boid>();
                neighbours.neighbours.resize(numBoids);

                if constexpr (config::EVAL_MODE == config::EvalMode::StructureOfArrays) {
                    soa.pos_x.resize(numBoids);
                    soa.pos_y.resize(numBoids);
                    soa.pos_z.resize(numBoids);
                    soa.vel_x.resize(numBoids);
                    soa.vel_y.resize(numBoids);
                    soa.vel_z.resize(numBoids);
                    soa.config_sep_weight.resize(numBoids);
                    soa.config_ali_weight.resize(numBoids);
                    soa.config_coh_weight.resize(numBoids);
                    soa.perception_sep_radius_sq.resize(numBoids);
                    soa.perception_ali_radius_sq.resize(numBoids);
                    soa.perception_coh_radius_sq.resize(numBoids);
                    soa.perception_fov_cos.resize(numBoids);
                    soa.boid_indices.resize(numBoids);

                    const auto* cfg = it.world().try_get<BoidConfig>();
                    const auto* per = it.world().try_get<BoidPerception>();
                    if (!cfg || !per) return;

                    for (size_t i = 0; i < numBoids; ++i) {
                        soa.config_sep_weight[i]        = cfg->separation_weight;
                        soa.config_ali_weight[i]        = cfg->alignment_weight;
                        soa.config_coh_weight[i]        = cfg->cohesion_weight;
                        soa.perception_sep_radius_sq[i] = per->separation_radius * per->separation_radius;
                        soa.perception_ali_radius_sq[i] = per->alignment_radius  * per->alignment_radius;
                        soa.perception_coh_radius_sq[i] = per->cohesion_radius   * per->cohesion_radius;
                        soa.perception_fov_cos[i]       = per->fov_cos;
                        soa.boid_indices[i]             = static_cast<int32_t>(i);
                    }
                }

                size_t index = 0;
                while (it.next()) {
                    auto pos  = it.field<const Position3D>(0);
                    auto vel  = it.field<const Velocity3D>(1);
                    auto boid = it.field<Boid>(2);

                    for (const uint64_t i : it) {
                        neighbours.neighbours[index].pos = pos[i].pos;
                        neighbours.neighbours[index].vel = vel[i].vel;

                        if constexpr (config::EVAL_MODE == config::EvalMode::StructureOfArrays) {
                            soa.pos_x[index] = pos[i].pos.x;
                            soa.pos_y[index] = pos[i].pos.y;
                            soa.pos_z[index] = pos[i].pos.z;
                            soa.vel_x[index] = vel[i].vel.x;
                            soa.vel_y[index] = vel[i].vel.y;
                            soa.vel_z[index] = vel[i].vel.z;
                        }

                        boid[i].scratch_index = static_cast<uint32_t>(index);
                        ++index;
                    }
                }
            });
    }

    // ------------------------------------------------------------------
    //  Steering system (OnUpdate) – AoS or SoA depending on EVAL_MODE
    // ------------------------------------------------------------------
    void register_boid_steering_system(const flecs::world &world)
    {
        world.system<
            const Position3D,
            const Velocity3D,
            const Boid,
            DesiredVelocity3D>
        ("BoidSteering")
        .kind(flecs::OnUpdate)
        .multi_threaded()
        .run([](flecs::iter& it) {

            const auto* cfg = it.world().try_get<BoidConfig>();
            const auto* per = it.world().try_get<BoidPerception>();
            const auto& neighbours = it.world().get<NeighbourList>();
            if (!cfg || !per) return;

            if constexpr (config::EVAL_MODE == config::EvalMode::ArrayOfStructures) {
                // AoS scalar path
                while (it.next()) {
                    auto pos  = it.field<const Position3D>(0);
                    auto vel  = it.field<const Velocity3D>(1);
                    auto boid = it.field<const Boid>(2);
                    auto out  = it.field<DesiredVelocity3D>(3);

                    for (const uint64_t i : it) {
                        const uint32_t self = boid[i].scratch_index;
                        boid::SteeringInput in{
                            pos[i].pos,
                            vel[i].vel,
                            *cfg,
                            *per,
                            { neighbours.neighbours.data(), static_cast<uint32_t>(neighbours.neighbours.size()) },
                            self
                        };

                        const auto forces = boid::compute_steering_forces_AoS(in);
                        const Vec3 totalSteer = boid::combine_steering(
                            { forces.separation, forces.alignment, forces.cohesion },
                            cfg->max_force
                        );
                        out[i].desired = boid::desired_velocity(vel[i].vel, totalSteer);
                    }
                }
            } else {
                // SoA SIMD path
                const auto& soa = it.world().get<BoidScratchpadSoA>();
                if (soa.count == 0) return;

                boid::SteeringInputSoA inSoA{};
                inSoA.num_boids                = soa.count;
                inSoA.pos_x                    = soa.pos_x.data();
                inSoA.pos_y                    = soa.pos_y.data();
                inSoA.pos_z                    = soa.pos_z.data();
                inSoA.vel_x                    = soa.vel_x.data();
                inSoA.vel_y                    = soa.vel_y.data();
                inSoA.vel_z                    = soa.vel_z.data();
                inSoA.config_sep_weight        = soa.config_sep_weight.data();
                inSoA.config_ali_weight        = soa.config_ali_weight.data();
                inSoA.config_coh_weight        = soa.config_coh_weight.data();
                inSoA.perception_sep_radius_sq = soa.perception_sep_radius_sq.data();
                inSoA.perception_ali_radius_sq = soa.perception_ali_radius_sq.data();
                inSoA.perception_coh_radius_sq = soa.perception_coh_radius_sq.data();
                inSoA.perception_fov_cos       = soa.perception_fov_cos.data();
                inSoA.boid_indices             = soa.boid_indices.data();
                inSoA.neighbours               = neighbours.neighbours.data();
                inSoA.num_neighbours           = neighbours.neighbours.size();

                alignas(64) float sep_x[config::MAX_BOIDS];
                alignas(64) float sep_y[config::MAX_BOIDS];
                alignas(64) float sep_z[config::MAX_BOIDS];
                alignas(64) float ali_x[config::MAX_BOIDS];
                alignas(64) float ali_y[config::MAX_BOIDS];
                alignas(64) float ali_z[config::MAX_BOIDS];
                alignas(64) float coh_x[config::MAX_BOIDS];
                alignas(64) float coh_y[config::MAX_BOIDS];
                alignas(64) float coh_z[config::MAX_BOIDS];

                inSoA.sep_x = sep_x;
                inSoA.sep_y = sep_y;
                inSoA.sep_z = sep_z;
                inSoA.ali_x = ali_x;
                inSoA.ali_y = ali_y;
                inSoA.ali_z = ali_z;
                inSoA.coh_x = coh_x;
                inSoA.coh_y = coh_y;
                inSoA.coh_z = coh_z;

                boid::compute_steering_forces_SoA(inSoA);

                while (it.next()) {
                    auto vel  = it.field<const Velocity3D>(1);
                    auto boid = it.field<const Boid>(2);
                    auto out  = it.field<DesiredVelocity3D>(3);

                    for (const uint64_t i : it) {
                        const uint32_t idx = boid[i].scratch_index;
                        Vec3 sep{ sep_x[idx], sep_y[idx], sep_z[idx] };
                        Vec3 ali{ ali_x[idx], ali_y[idx], ali_z[idx] };
                        Vec3 coh{ coh_x[idx], coh_y[idx], coh_z[idx] };
                        const Vec3 totalSteer = boid::combine_steering(
                            { sep, ali, coh }, cfg->max_force
                        );
                        out[i].desired = boid::desired_velocity(vel[i].vel, totalSteer);
                    }
                }
            }
        });
    }

    // ------------------------------------------------------------------
    //  Apply velocity (PostUpdate)
    // ------------------------------------------------------------------
    void register_boid_apply_velocity(const flecs::world &world)
    {
        world.system<Velocity3D, const DesiredVelocity3D>("ApplyVelocity")
            .kind(flecs::PostUpdate)
            .multi_threaded()
            .run([](flecs::iter& it) {

                const BoidConfig* cfg = it.world().try_get<BoidConfig>();
                if (!cfg) return;

                while (it.next()) {
                    auto vel_out      = it.field<Velocity3D>(0);
                    auto desired_in   = it.field<const DesiredVelocity3D>(1);

                    for (const uint64_t i : it) {
                        Vec3 desired = desired_in[i].desired;
                        const float len2 = desired.length_sq();

                        if (len2 > cfg->max_speed * cfg->max_speed) {
                            desired = desired.normalized() * cfg->max_speed;
                        } else if (len2 < 1e-10f) {
                            continue;
                        }

                        vel_out[i].vel = desired;
                    }
                }
            });
    }

    // ------------------------------------------------------------------
    //  Integrate position (PostUpdate)
    // ------------------------------------------------------------------
    void register_boid_integrate_position(const flecs::world &world)
    {
        world.system<Position3D, const Velocity3D>("IntegratePosition")
            .kind(flecs::PostUpdate)
            .multi_threaded()
            .run([](flecs::iter& it) {

                constexpr float dt = static_cast<float>(config::SIM_DT.count()) / 1'000'000'000.0f;

                while (it.next()) {
                    auto pos_out = it.field<Position3D>(0);
                    auto vel_in  = it.field<const Velocity3D>(1);

                    for (const uint64_t i : it) {
                        pos_out[i].pos += vel_in[i].vel * dt;
                        Vec3::wrap_position(pos_out[i].pos, config::WORLD_HALF);
                    }
                }
            });
    }

} // namespace simnet::ecs

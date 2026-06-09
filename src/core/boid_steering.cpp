#include "config.hpp"
#include <algorithm>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "boid_steering.cpp"
#include <hwy/foreach_target.h>

#include <hwy/highway.h>
#include "boid_steering.hpp"


// -------------------------------------------------
// Per‑target SIMD implementation (compiled multiple times)
// -------------------------------------------------
namespace simnet::boid {
    namespace HWY_NAMESPACE {

     namespace hn = hwy::HWY_NAMESPACE;

    HWY_ATTR void ComputeSteeringForcesSoA_Impl(const SteeringInputSoA& in) {
        const hn::ScalableTag<float> d;
        const size_t N = hn::Lanes(d);

        const float world_size = 2.0f * config::WORLD_HALF;
        const float half_world = config::WORLD_HALF;

        const hn::Vec<decltype(d)> vworld   = hn::Set(d, world_size);
        const hn::Vec<decltype(d)> vhalf    = hn::Set(d, half_world);
        const hn::Vec<decltype(d)> vepsilon = hn::Set(d, 1e-10f);

        size_t i = 0;
        for (; i + N <= in.num_boids; i += N) {
            // --- Load boid state ---
            const auto px = hn::LoadU(d, in.pos_x + i);
            const auto py = hn::LoadU(d, in.pos_y + i);
            const auto pz = hn::LoadU(d, in.pos_z + i);
            const auto vx = hn::LoadU(d, in.vel_x + i);
            const auto vy = hn::LoadU(d, in.vel_y + i);
            const auto vz = hn::LoadU(d, in.vel_z + i);

            // Config & perception
            const auto sep_w = hn::LoadU(d, in.config_sep_weight + i);
            const auto ali_w = hn::LoadU(d, in.config_ali_weight + i);
            const auto coh_w = hn::LoadU(d, in.config_coh_weight + i);
            const auto sep_r2 = hn::LoadU(d, in.perception_sep_radius_sq + i);
            const auto ali_r2 = hn::LoadU(d, in.perception_ali_radius_sq + i);
            const auto coh_r2 = hn::LoadU(d, in.perception_coh_radius_sq + i);
            const auto fov_cos = in.perception_fov_cos
                                 ? hn::LoadU(d, in.perception_fov_cos + i)
                                 : hn::Set(d, -2.0f);
            const auto needs_fov = hn::Gt(fov_cos, hn::Set(d, -1.0f));

            // Self-indices
            const auto self_idx = hn::Iota(d, static_cast<int32_t>(i));

            // Accumulators
            auto sep_x_acc = hn::Zero(d);
            auto sep_y_acc = hn::Zero(d);
            auto sep_z_acc = hn::Zero(d);
            auto sep_cnt    = hn::Zero(d);

            auto ali_x_acc = hn::Zero(d);
            auto ali_y_acc = hn::Zero(d);
            auto ali_z_acc = hn::Zero(d);
            auto ali_cnt    = hn::Zero(d);

            auto coh_x_acc = hn::Zero(d);
            auto coh_y_acc = hn::Zero(d);
            auto coh_z_acc = hn::Zero(d);
            auto coh_cnt    = hn::Zero(d);

            // Forward direction
            const auto vel_len_sq = hn::MulAdd(vx, vx, hn::MulAdd(vy, vy, hn::Mul(vz, vz)));
            const auto inv_vel_len = hn::ApproximateReciprocalSqrt(vel_len_sq);
            const auto fwd_x = hn::Mul(vx, inv_vel_len);
            const auto fwd_y = hn::Mul(vy, inv_vel_len);
            const auto fwd_z = hn::Mul(vz, inv_vel_len);

            // --- Neighbour loop ---
            for (size_t j = 0; j < in.num_neighbours; ++j) {
                const ecs::AoSNeighbour& n = in.neighbours[j];
                const auto npx = hn::Set(d, n.pos.x);
                const auto npy = hn::Set(d, n.pos.y);
                const auto npz = hn::Set(d, n.pos.z);
                const auto nvx = hn::Set(d, n.vel.x);
                const auto nvy = hn::Set(d, n.vel.y);
                const auto nvz = hn::Set(d, n.vel.z);

                // Delta with wrapping
                auto dx = hn::Sub(npx, px);
                auto dy = hn::Sub(npy, py);
                auto dz = hn::Sub(npz, pz);

                auto qx = hn::Mul(hn::Round(hn::Div(dx, vworld)), vworld);
                dx = hn::Sub(dx, qx);
                auto qy = hn::Mul(hn::Round(hn::Div(dy, vworld)), vworld);
                dy = hn::Sub(dy, qy);
                auto qz = hn::Mul(hn::Round(hn::Div(dz, vworld)), vworld);
                dz = hn::Sub(dz, qz);

                const auto distSq = hn::MulAdd(dx, dx,
                                    hn::MulAdd(dy, dy,
                                        hn::Mul(dz, dz)));

                // Validity mask
                auto valid = hn::Gt(distSq, vepsilon);
                valid = hn::And(valid,
                        hn::Not(hn::Eq(self_idx, hn::Set(d, static_cast<int32_t>(j)))));

                // FOV check
                if (hn::AllTrue(d, needs_fov)) {
                    const auto invDist = hn::ApproximateReciprocalSqrt(distSq);
                    const auto dir_x = hn::Mul(dx, invDist);
                    const auto dir_y = hn::Mul(dy, invDist);
                    const auto dir_z = hn::Mul(dz, invDist);
                    const auto dot = hn::MulAdd(fwd_x, dir_x,
                                      hn::MulAdd(fwd_y, dir_y,
                                          hn::Mul(fwd_z, dir_z)));
                    const auto fov_ok = hn::Gt(dot, fov_cos);
                    valid = hn::And(valid, fov_ok);
                }

                // Separation
                const auto inSep = hn::And(valid, hn::Lt(distSq, sep_r2));
                sep_x_acc = hn::Add(sep_x_acc, hn::IfThenElseZero(inSep, hn::Neg(dx)));
                sep_y_acc = hn::Add(sep_y_acc, hn::IfThenElseZero(inSep, hn::Neg(dy)));
                sep_z_acc = hn::Add(sep_z_acc, hn::IfThenElseZero(inSep, hn::Neg(dz)));
                sep_cnt = hn::Add(sep_cnt, hn::IfThenElseZero(inSep, hn::Set(d, 1.0f)));

                // Alignment
                const auto inAli = hn::And(valid, hn::Lt(distSq, ali_r2));
                ali_x_acc = hn::Add(ali_x_acc, hn::IfThenElseZero(inAli, nvx));
                ali_y_acc = hn::Add(ali_y_acc, hn::IfThenElseZero(inAli, nvy));
                ali_z_acc = hn::Add(ali_z_acc, hn::IfThenElseZero(inAli, nvz));
                ali_cnt = hn::Add(ali_cnt, hn::IfThenElseZero(inAli, hn::Set(d, 1.0f)));

                // Cohesion
                const auto inCoh = hn::And(valid, hn::Lt(distSq, coh_r2));
                coh_x_acc = hn::Add(coh_x_acc, hn::IfThenElseZero(inCoh, npx));
                coh_y_acc = hn::Add(coh_y_acc, hn::IfThenElseZero(inCoh, npy));
                coh_z_acc = hn::Add(coh_z_acc, hn::IfThenElseZero(inCoh, npz));
                coh_cnt = hn::Add(coh_cnt, hn::IfThenElseZero(inCoh, hn::Set(d, 1.0f)));
            }

            // --- Finalise forces ---

            // Separation
            auto sep_mask = hn::Gt(sep_cnt, hn::Zero(d));
            auto sep_cnt_safe = hn::IfThenElse(sep_mask, sep_cnt, hn::Set(d, 1.0f));
            auto out_sep_x = hn::Div(sep_x_acc, sep_cnt_safe);
            auto out_sep_y = hn::Div(sep_y_acc, sep_cnt_safe);
            auto out_sep_z = hn::Div(sep_z_acc, sep_cnt_safe);
            out_sep_x = hn::Mul(hn::IfThenElseZero(sep_mask, out_sep_x), sep_w);
            out_sep_y = hn::Mul(hn::IfThenElseZero(sep_mask, out_sep_y), sep_w);
            out_sep_z = hn::Mul(hn::IfThenElseZero(sep_mask, out_sep_z), sep_w);
            hn::StoreU(out_sep_x, d, in.sep_x + i);
            hn::StoreU(out_sep_y, d, in.sep_y + i);
            hn::StoreU(out_sep_z, d, in.sep_z + i);

            // Alignment
            auto ali_mask = hn::Gt(ali_cnt, hn::Zero(d));
            auto ali_cnt_safe = hn::IfThenElse(ali_mask, ali_cnt, hn::Set(d, 1.0f));
            auto out_ali_x = hn::Div(ali_x_acc, ali_cnt_safe);
            auto out_ali_y = hn::Div(ali_y_acc, ali_cnt_safe);
            auto out_ali_z = hn::Div(ali_z_acc, ali_cnt_safe);
            auto ali_force_x = hn::Sub(out_ali_x, vx);
            auto ali_force_y = hn::Sub(out_ali_y, vy);
            auto ali_force_z = hn::Sub(out_ali_z, vz);
            out_ali_x = hn::Mul(hn::IfThenElseZero(ali_mask, ali_force_x), ali_w);
            out_ali_y = hn::Mul(hn::IfThenElseZero(ali_mask, ali_force_y), ali_w);
            out_ali_z = hn::Mul(hn::IfThenElseZero(ali_mask, ali_force_z), ali_w);
            hn::StoreU(out_ali_x, d, in.ali_x + i);
            hn::StoreU(out_ali_y, d, in.ali_y + i);
            hn::StoreU(out_ali_z, d, in.ali_z + i);

            // Cohesion
            auto coh_mask = hn::Gt(coh_cnt, hn::Zero(d));
            auto coh_cnt_safe = hn::IfThenElse(coh_mask, coh_cnt, hn::Set(d, 1.0f));
            auto out_coh_x = hn::Div(coh_x_acc, coh_cnt_safe);
            auto out_coh_y = hn::Div(coh_y_acc, coh_cnt_safe);
            auto out_coh_z = hn::Div(coh_z_acc, coh_cnt_safe);
            auto coh_force_x = hn::Sub(out_coh_x, px);
            auto coh_force_y = hn::Sub(out_coh_y, py);
            auto coh_force_z = hn::Sub(out_coh_z, pz);
            out_coh_x = hn::Mul(hn::IfThenElseZero(coh_mask, coh_force_x), coh_w);
            out_coh_y = hn::Mul(hn::IfThenElseZero(coh_mask, coh_force_y), coh_w);
            out_coh_z = hn::Mul(hn::IfThenElseZero(coh_mask, coh_force_z), coh_w);
            hn::StoreU(out_coh_x, d, in.coh_x + i);
            hn::StoreU(out_coh_y, d, in.coh_y + i);
            hn::StoreU(out_coh_z, d, in.coh_z + i);
        }

        // --- Scalar tail ---
        for (; i < in.num_boids; ++i) {
            const Vec3 pos(in.pos_x[i], in.pos_y[i], in.pos_z[i]);
            const Vec3 vel(in.vel_x[i], in.vel_y[i], in.vel_z[i]);

            const float sepWeight = in.config_sep_weight[i];
            const float aliWeight = in.config_ali_weight[i];
            const float cohWeight = in.config_coh_weight[i];
            const float sepR = std::sqrt(in.perception_sep_radius_sq[i]);
            const float aliR = std::sqrt(in.perception_ali_radius_sq[i]);
            const float cohR = std::sqrt(in.perception_coh_radius_sq[i]);
            const float fovCos = in.perception_fov_cos ? in.perception_fov_cos[i] : -2.0f;

            Vec3 sepSum(0,0,0), aliSum(0,0,0), cohSum(0,0,0);
            int sepCnt = 0, aliCnt = 0, cohCnt = 0;

            const Vec3 forward = Vec3::forward_direction(vel);
            const float sepR2 = sepR * sepR;
            const float aliR2 = aliR * aliR;
            const float cohR2 = cohR * cohR;
            const float maxR2 = std::max({sepR2, aliR2, cohR2});

            for (size_t j = 0; j < in.num_neighbours; ++j) {
                if (static_cast<int32_t>(j) == i) continue;
                const ecs::AoSNeighbour& n = in.neighbours[j];

                Vec3 delta = Vec3::wrap_delta(n.pos, pos, config::WORLD_HALF);
                const float distSq = delta.length_sq();
                if (distSq < 1e-10f || distSq > maxR2) continue;

                if (fovCos > -1.0f) {
                    const float invDist = 1.0f / std::sqrt(distSq);
                    const Vec3 dir = delta * invDist;
                    if (Vec3::dot(forward, dir) < fovCos) continue;
                }

                if (distSq < sepR2) {
                    sepSum = sepSum - delta;
                    ++sepCnt;
                }
                if (distSq < aliR2) {
                    aliSum = aliSum + n.vel;
                    ++aliCnt;
                }
                if (distSq < cohR2) {
                    cohSum = cohSum + (pos + delta);
                    ++cohCnt;
                }
            }

            if (sepCnt > 0) sepSum = (sepSum / static_cast<float>(sepCnt)) * sepWeight;
            if (aliCnt > 0) aliSum = ((aliSum / static_cast<float>(aliCnt)) - vel) * aliWeight;
            if (cohCnt > 0) cohSum = ((cohSum / static_cast<float>(cohCnt)) - pos) * cohWeight;

            in.sep_x[i] = sepSum.x; in.sep_y[i] = sepSum.y; in.sep_z[i] = sepSum.z;
            in.ali_x[i] = aliSum.x; in.ali_y[i] = aliSum.y; in.ali_z[i] = aliSum.z;
            in.coh_x[i] = cohSum.x; in.coh_y[i] = cohSum.y; in.coh_z[i] = cohSum.z;
        }
    }

    }
}

// -------------------------------------------------
// Only‑once section (dynamic dispatch, AoS scalar, helpers)
// -------------------------------------------------
#if HWY_ONCE
namespace simnet::boid {

    HWY_EXPORT(ComputeSteeringForcesSoA_Impl);

    void compute_steering_forces_SoA(const SteeringInputSoA& in) {
        return HWY_DYNAMIC_DISPATCH(ComputeSteeringForcesSoA_Impl)(in);
    }

    SteeringForces compute_steering_forces_AoS(const SteeringInput& in)
    {
        SteeringForces forces{};

        int sepCount = 0;
        int aliCount = 0;
        int cohCount = 0;

        const Vec3 forward = Vec3::forward_direction(in.velocity);

        const float separationRadiusSq = in.perception.separation_radius * in.perception.separation_radius;
        const float alignmentRadiusSq  = in.perception.alignment_radius * in.perception.alignment_radius;
        const float cohesionRadiusSq   = in.perception.cohesion_radius * in.perception.cohesion_radius;
        const float maxRadiusSq        = std::max({ separationRadiusSq, alignmentRadiusSq, cohesionRadiusSq });

        constexpr float epsilon = 1e-10f;

        for (uint32_t j = 0; j < in.scratchpadAoS.count; ++j)
        {
            if (j == in.self_index)
                continue;

            const Vec3 neighborPos = in.scratchpadAoS.neighbours[j].pos;

            Vec3 delta = Vec3::wrap_delta(neighborPos, in.position, config::WORLD_HALF);
            const float distSq = delta.length_sq();

            if (distSq < epsilon || distSq > maxRadiusSq)
                continue;

            if (in.perception.fov_cos > -1.0f)
            {
                const float invDist = 1.0f / std::sqrt(distSq);
                const Vec3 dir = delta * invDist;

                if (Vec3::dot(forward, dir) < in.perception.fov_cos)
                    continue;
            }

            // Separation
            if (in.config.separation_weight != 0.0f && distSq < separationRadiusSq)
            {
                forces.separation -= delta;
                ++sepCount;
            }

            // Alignment
            if (in.config.alignment_weight != 0.0f && distSq < alignmentRadiusSq)
            {
                forces.alignment += in.scratchpadAoS.neighbours[j].vel;
                ++aliCount;
            }

            // Cohesion
            if (in.config.cohesion_weight != 0.0f && distSq < cohesionRadiusSq)
            {
                forces.cohesion += in.position + delta;
                ++cohCount;
            }
        }

        if (sepCount > 0) forces.separation = (forces.separation / static_cast<float>(sepCount)) * in.config.separation_weight;
        if (aliCount > 0) forces.alignment  = ((forces.alignment / static_cast<float>(aliCount)) - in.velocity) * in.config.alignment_weight;
        if (cohCount > 0) forces.cohesion   = ((forces.cohesion / static_cast<float>(cohCount)) - in.position) * in.config.cohesion_weight;

        return forces;
    }

    Vec3 combine_steering(
        const std::initializer_list<Vec3> forces,
        const float max_force) noexcept
    {
        Vec3 total{};

        for (const Vec3& force : forces)
        {
            total += force;
        }

        const float len = total.length();

        if (len > max_force && len > 1e-10f)
        {
            total *= (max_force / len);
        }

        return total;
    }

    Vec3 desired_velocity(
        const Vec3& current_vel,
        const Vec3& total_steer) noexcept
    {
        return current_vel + total_steer;
    }

}  // namespace simnet::boid
#endif  // HWY_ONCE
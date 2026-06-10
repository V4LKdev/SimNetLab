#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "boid/boid_steering.cpp"
#include "boid_steering.hpp"

#include <hwy/aligned_allocator.h>
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

#include "telemetry.hpp"

// -------------------------------------------------
// Per‑target SIMD implementation (compiled multiple times)
// -------------------------------------------------
namespace simnet::boid {
    namespace
    HWY_NAMESPACE {
        namespace hn = hwy::HWY_NAMESPACE;

        template<class V>
        HWY_ATTR Vec3 ReduceVec3(hn::ScalableTag<float> d, V vx, V vy, V vz)
        {
            return {
                hn::GetLane(hn::SumOfLanes(d, vx)),
                hn::GetLane(hn::SumOfLanes(d, vy)),
                hn::GetLane(hn::SumOfLanes(d, vz))
            };
        }

        HWY_ATTR void ComputeBoidSteering_Impl(std::uint64_t n,
                                               const ecs::Position * HWY_RESTRICT pos,
                                               const ecs::Velocity * HWY_RESTRICT vel,
                                               const ecs::BoidConfig &cfg,
                                               const ecs::BoidPerception &per,
                                               ecs::SteeringAccumulate * HWY_RESTRICT out,
                                               const NeighborQuery &query)
        {
            TELEM_TRACY_ZONE("ComputeBoidSteering_Impl");

            const hn::ScalableTag<float> d;
            const size_t N = Lanes(d);
            const hn::Vec<decltype(d)> v_zero = hn::Zero(d);


            const float max_speed = cfg.max_speed;
            const float max_force = cfg.max_force;
            const float sep_weight = cfg.separation_weight;
            const float ali_weight = cfg.alignment_weight;
            const float coh_weight = cfg.cohesion_weight;

            const float sep_r2 = per.separation_radius * per.separation_radius;
            const float ali_r2 = per.alignment_radius * per.alignment_radius;
            const float coh_r2 = per.cohesion_radius * per.cohesion_radius;

            // Largest radius for neighbor gathering
            const float max_nb_dist2 = std::max({sep_r2, ali_r2, coh_r2});

            const bool do_sep = (sep_weight != 0.0f);
            const bool do_ali = (ali_weight != 0.0f);
            const bool do_coh = (coh_weight != 0.0f);


            std::vector<Neighbor> neighbors_vec(n);
            Neighbor *neighbors = neighbors_vec.data();

            // SoA arrays for SIMD processing
            hwy::AlignedFreeUniquePtr<float[]> nx = hwy::AllocateAligned<float>(n);
            hwy::AlignedFreeUniquePtr<float[]> ny = hwy::AllocateAligned<float>(n);
            hwy::AlignedFreeUniquePtr<float[]> nz = hwy::AllocateAligned<float>(n);
            hwy::AlignedFreeUniquePtr<float[]> nvx = hwy::AllocateAligned<float>(n);
            hwy::AlignedFreeUniquePtr<float[]> nvy = hwy::AllocateAligned<float>(n);
            hwy::AlignedFreeUniquePtr<float[]> nvz = hwy::AllocateAligned<float>(n);

            TELEM_TRACY_ALLOC(nx.get(), n * sizeof(float));
            TELEM_TRACY_ALLOC(ny.get(), n * sizeof(float));
            TELEM_TRACY_ALLOC(nz.get(), n * sizeof(float));
            TELEM_TRACY_ALLOC(nvx.get(), n * sizeof(float));
            TELEM_TRACY_ALLOC(nvy.get(), n * sizeof(float));
            TELEM_TRACY_ALLOC(nvz.get(), n * sizeof(float));


            for (std::uint64_t i = 0; i < n; ++i) {
                // 3a. Gather neighbors
                const std::uint32_t m = query.fn(pos[i], pos, vel,
                                                 static_cast<std::uint32_t>(n),
                                                 static_cast<std::uint32_t>(i),
                                                 neighbors,
                                                 static_cast<std::uint32_t>(n),
                                                 max_nb_dist2);

                // self data (needed for both paths)
                const float px_self = pos[i].value.x;
                const float py_self = pos[i].value.y;
                const float pz_self = pos[i].value.z;
                const float vx_self = vel[i].value.x;
                const float vy_self = vel[i].value.y;
                const float vz_self = vel[i].value.z;

                // variables that will hold the steering data (initialised to zero)
                Vec3 sep_avg{0, 0, 0}, ali_avg{0, 0, 0}, coh_avg{0, 0, 0};
                uint32_t n_sep = 0, n_ali = 0, n_coh = 0;
                Vec3 steering{0, 0, 0};

                if (m == 0) {
                    // No neighbors -> keep current velocity, clamped
                    const Vec3 &v = vel[i].value;
                    float speed2 = v.x * v.x + v.y * v.y + v.z * v.z;
                    if (speed2 > max_speed * max_speed) {
                        float scale = max_speed / std::sqrt(speed2);
                        out[i].value = {v.x * scale, v.y * scale, v.z * scale};
                    } else {
                        out[i].value = v;
                    }
                    // steering components stay zero – no neighbors
                } else {
                    // 3c. Scatter neighbor positions/velocities to SoA arrays
                    for (std::uint32_t j = 0; j < m; ++j) {
                        const auto &np = neighbors[j].pos.value;
                        const auto &nv = neighbors[j].vel.value;
                        nx[j] = np.x;
                        ny[j] = np.y;
                        nz[j] = np.z;
                        nvx[j] = nv.x;
                        nvy[j] = nv.y;
                        nvz[j] = nv.z;
                    }

                    // ------------------------------------------------------------------
                    // 3e. SIMD accumulation over neighbors
                    // ------------------------------------------------------------------
                    auto sep_sum_x = v_zero, sep_sum_y = v_zero, sep_sum_z = v_zero;
                    auto ali_sum_x = v_zero, ali_sum_y = v_zero, ali_sum_z = v_zero;
                    auto coh_sum_x = v_zero, coh_sum_y = v_zero, coh_sum_z = v_zero;

                    const auto v_px_self = hn::Set(d, px_self);
                    const auto v_py_self = hn::Set(d, py_self);
                    const auto v_pz_self = hn::Set(d, pz_self);
                    const auto v_vx_self = hn::Set(d, vx_self);
                    const auto v_vy_self = hn::Set(d, vy_self);
                    const auto v_vz_self = hn::Set(d, vz_self);

                    const auto v_sep_r2 = hn::Set(d, sep_r2);
                    const auto v_ali_r2 = hn::Set(d, ali_r2);
                    const auto v_coh_r2 = hn::Set(d, coh_r2);

                    uint32_t k = 0;
                    for (; k + N <= m; k += N) {
                        // Load N neighbors' data
                        const auto nx_v = hn::LoadU(d, nx.get() + k);
                        const auto ny_v = hn::LoadU(d, ny.get() + k);
                        const auto nz_v = hn::LoadU(d, nz.get() + k);
                        const auto nvx_v = hn::LoadU(d, nvx.get() + k);
                        const auto nvy_v = hn::LoadU(d, nvy.get() + k);
                        const auto nvz_v = hn::LoadU(d, nvz.get() + k);

                        // Offset
                        const auto dx = nx_v - v_px_self;
                        const auto dy = ny_v - v_py_self;
                        const auto dz = nz_v - v_pz_self;

                        // Squared distance
                        const auto dist2 = dx * dx + dy * dy + dz * dz;

                        // Masks
                        const auto mask_sep = do_sep ? (dist2 < v_sep_r2) : hn::MaskFalse(d);
                        const auto mask_ali = do_ali ? (dist2 < v_ali_r2) : hn::MaskFalse(d);
                        const auto mask_coh = do_coh ? (dist2 < v_coh_r2) : hn::MaskFalse(d);

                        // --- Separation ---
                        if (do_sep) {
                            const auto safe_dist2 = hn::Max(dist2, hn::Set(d, 1e-10f));

                            const auto inv_dist2 = hn::Div(hn::Set(d, 1.0f), safe_dist2);

                            const auto sep_x = (v_px_self - nx_v) * inv_dist2;
                            const auto sep_y = (v_py_self - ny_v) * inv_dist2;
                            const auto sep_z = (v_pz_self - nz_v) * inv_dist2;

                            sep_sum_x = hn::Add(sep_sum_x, hn::IfThenElseZero(mask_sep, sep_x));
                            sep_sum_y = hn::Add(sep_sum_y, hn::IfThenElseZero(mask_sep, sep_y));
                            sep_sum_z = hn::Add(sep_sum_z, hn::IfThenElseZero(mask_sep, sep_z));
                            n_sep += hn::CountTrue(d, mask_sep);
                        }

                        // --- Alignment ---
                        if (do_ali) {
                            ali_sum_x = hn::Add(ali_sum_x, hn::IfThenElseZero(mask_ali, nvx_v));
                            ali_sum_y = hn::Add(ali_sum_y, hn::IfThenElseZero(mask_ali, nvy_v));
                            ali_sum_z = hn::Add(ali_sum_z, hn::IfThenElseZero(mask_ali, nvz_v));
                            n_ali += hn::CountTrue(d, mask_ali);
                        }

                        // --- Cohesion ---
                        if (do_coh) {
                            coh_sum_x = hn::Add(coh_sum_x, hn::IfThenElseZero(mask_coh, nx_v));
                            coh_sum_y = hn::Add(coh_sum_y, hn::IfThenElseZero(mask_coh, ny_v));
                            coh_sum_z = hn::Add(coh_sum_z, hn::IfThenElseZero(mask_coh, nz_v));
                            n_coh += hn::CountTrue(d, mask_coh);
                        }
                    }

                    // remainder scalar
                    float sep_rx = 0, sep_ry = 0, sep_rz = 0;
                    float ali_rx = 0, ali_ry = 0, ali_rz = 0;
                    float coh_rx = 0, coh_ry = 0, coh_rz = 0;

                    for (; k < m; ++k) {
                        float dx = nx[k] - px_self;
                        float dy = ny[k] - py_self;
                        float dz = nz[k] - pz_self;
                        float d2 = dx * dx + dy * dy + dz * dz;

                        if (do_sep && d2 < sep_r2) {
                            float inv_d2 = 1.0f / std::max(d2, 1e-10f);
                            sep_rx += (px_self - nx[k]) * inv_d2;
                            sep_ry += (py_self - ny[k]) * inv_d2;
                            sep_rz += (pz_self - nz[k]) * inv_d2;
                            ++n_sep;
                        }
                        if (do_ali && d2 < ali_r2) {
                            ali_rx += nvx[k];
                            ali_ry += nvy[k];
                            ali_rz += nvz[k];
                            ++n_ali;
                        }
                        if (do_coh && d2 < coh_r2) {
                            coh_rx += nx[k];
                            coh_ry += ny[k];
                            coh_rz += nz[k];
                            ++n_coh;
                        }
                    }

                    // 3f. Horizontal reduction
                    sep_avg = n_sep > 0 ? ReduceVec3(d, sep_sum_x, sep_sum_y, sep_sum_z) : Vec3{0, 0, 0};
                    ali_avg = n_ali > 0 ? ReduceVec3(d, ali_sum_x, ali_sum_y, ali_sum_z) : Vec3{0, 0, 0};
                    coh_avg = n_coh > 0 ? ReduceVec3(d, coh_sum_x, coh_sum_y, coh_sum_z) : Vec3{0, 0, 0};

                    // Fold in scalar remainders
                    sep_avg.x += sep_rx;
                    sep_avg.y += sep_ry;
                    sep_avg.z += sep_rz;
                    ali_avg.x += ali_rx;
                    ali_avg.y += ali_ry;
                    ali_avg.z += ali_rz;
                    coh_avg.x += coh_rx;
                    coh_avg.y += coh_ry;
                    coh_avg.z += coh_rz;

                    Vec3 steering{0, 0, 0};
                    float remaining_force = max_force;
                    const float eps = 1e-10f;

                    // --- 1. Separation ---
                    if (n_sep > 0) {
                    }

                    // --- 2. Alignment ---
                    if (n_ali > 0 && remaining_force > 0.0f) {
                    }

                    // --- 3. Cohesion ---
                    if (n_coh > 0 && remaining_force > 0.0f) {
                    }

                    float dvx = vx_self + steering.x;
                    float dvy = vy_self + steering.y;
                    float dvz = vz_self + steering.z;
                    float dv2 = dvx * dvx + dvy * dvy + dvz * dvz;
                    if (dv2 > max_speed * max_speed) {
                        float scale = max_speed / std::sqrt(dv2 + eps);
                        dvx *= scale;
                        dvy *= scale;
                        dvz *= scale;
                    }
                    out[i].value = {dvx, dvy, dvz};
                }
            }

            TELEM_TRACY_FREE(nx.get());
            TELEM_TRACY_FREE(ny.get());
            TELEM_TRACY_FREE(nz.get());
            TELEM_TRACY_FREE(nvx.get());
            TELEM_TRACY_FREE(nvy.get());
            TELEM_TRACY_FREE(nvz.get());
        }
    }
}

// -------------------------------------------------
// Only‑once section
// -------------------------------------------------
#if HWY_ONCE
namespace simnet::boid {
    HWY_EXPORT(ComputeBoidSteering_Impl);

    void compute_boid_steering(const ecs::Position *pos,
                               const ecs::Velocity *vel,
                               ecs::SteeringAccumulate *out,
                               std::uint64_t count,
                               const ecs::BoidConfig &config,
                               const ecs::BoidPerception &perception,
                               const NeighborQuery &query)
    {
        HWY_DYNAMIC_DISPATCH(ComputeBoidSteering_Impl)(count, pos, vel, config, perception, out, query);
    }
} // namespace simnet::boid
#endif // HWY_ONCE

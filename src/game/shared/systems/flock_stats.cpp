#include "telemetry.hpp"
#include <flecs.h>
#include "../components.hpp"

namespace simnet::ecs {
    void flock_statistics_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_FlockStats");

        const NeighborCache &snap = it.world().get<NeighborCache>();

        while (it.next()) {
            auto pos = it.field<const Position>(0);
            auto vel = it.field<const Velocity>(1);
            auto hd = it.field<const Heading>(2);
            auto acc = it.field<const SteeringAccumulate>(3);
            auto idx = it.field<const BoidIdx>(4);

            const int count = it.count();
            if (count == 0) continue;

            float sum_speed = 0.0f;
            float sum_steer = 0.0f;
            float sum_neighbors = 0.0f;
            float sum_min_dist = 0.0f;
            Vec3 avg_heading = Vec3::zero();

            for (size_t i = 0; i < count; ++i) {
                // Speed
                const float speed = vel[i].value.length();
                sum_speed += speed;

                // Steering magnitude
                sum_steer += acc[i].value.length();

                // Number of neighbours (from snapshot)
                const uint32_t g = idx[i].index;
                const size_t beg = snap.offsets[g];
                const size_t end = snap.offsets[g + 1];
                const size_t nbr_count = end - beg;
                sum_neighbors += static_cast<float>(nbr_count);

                // Closest neighbour distance
                float min_d2 = 1e10f;
                const Vec3 &self_pos = pos[i].value;
                for (size_t j = beg; j < end; ++j) {
                    const uint32_t nb_idx = snap.entries[j];
                    const float d2 = self_pos.dist_sq(snap.positions[nb_idx]);
                    if (d2 < min_d2) min_d2 = d2;
                }
                if (nbr_count > 0) {
                    sum_min_dist += std::sqrt(min_d2);
                }

                // Average heading
                avg_heading += hd[i].value;
            }

            avg_heading = avg_heading / static_cast<float>(count);
            const float polarisation = avg_heading.length(); // 1 = perfectly aligned

            FlockStats &stats = it.world().ensure<FlockStats>();
            stats.avg_speed = sum_speed / count;
            stats.avg_steer = sum_steer / count;
            stats.avg_neighbors = sum_neighbors / count;
            stats.min_nbr_dist = (count > 0) ? (sum_min_dist / count) : 0.0f;
            stats.polarisation = polarisation;
            stats.boid_count = count;

            TELEM_LOG_INFO("FLOCK | n={:3d} | speed={:5.1f} steer={:5.1f} nbr={:4.1f} "
                           "minDist={:4.1f} polar={:.2f}",
                           count, stats.avg_speed, stats.avg_steer, stats.avg_neighbors,
                           stats.min_nbr_dist, stats.polarisation);

            TELEM_TRACY_PLOT("EntityCount", static_cast<int64_t>(count));
        }
    }
}

#include "telemetry.hpp"
#include "ecs/components.hpp"
#include <flecs.h>

namespace simnet::ecs {
    void flock_statistics_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_FlockStats");

        // Log every 60 frames (≈1 sec at 60 fps) – adjust as needed
        static int frame = 0;
        if (++frame % 60 != 0) return;

        while (it.next()) {
            auto pos = it.field<const Position>(0);
            auto vel = it.field<const Velocity>(1);
            auto hd = it.field<const Heading>(2);
            auto acc = it.field<const SteeringAccumulate>(3);
            auto nl = it.field<const NeighborList>(4);

            const int count = it.count();
            if (count == 0) return;

            float sum_speed = 0.0f;
            float sum_steer = 0.0f;
            float sum_neighbors = 0.0f;
            float sum_min_dist = 0.0f;
            Vec3 avg_heading = Vec3::zero();

            for (int i = 0; i < count; ++i) {
                // Speed
                float speed = vel[i].value.length();
                sum_speed += speed;

                // Steering magnitude
                sum_steer += acc[i].value.length();

                // Number of neighbors
                const auto &nbs = nl[i].indices;
                sum_neighbors += static_cast<float>(nbs.size());

                // Closest neighbor distance
                float min_d2 = 1e10f;
                for (uint32_t nb_idx: nbs) {
                    float d2 = pos[i].value.dist2(pos[nb_idx].value);
                    if (d2 < min_d2) min_d2 = d2;
                }
                if (!nbs.empty()) sum_min_dist += std::sqrt(min_d2);

                // Average heading
                avg_heading += hd[i].value;
            }

            avg_heading = avg_heading / static_cast<float>(count);
            float polarisation = avg_heading.length(); // 1 = all same direction

            FlockStats &stats = it.world().ensure<FlockStats>();
            stats.avg_speed = sum_speed / count;
            stats.avg_steer = sum_steer / count;
            stats.avg_neighbors = sum_neighbors / count;
            stats.min_nbr_dist = (count > 0) ? sum_min_dist / count : 0.0f;
            stats.polarisation = polarisation;
            stats.boid_count = count;

            TELEM_LOG_INFO("FLOCK | n={:3d} | speed={:5.1f} steer={:5.1f} nbr={:4.1f} "
                           "minDist={:4.1f} polar={:.2f}",
                           count, stats.avg_speed, stats.avg_steer, stats.avg_neighbors,
                           stats.min_nbr_dist, stats.polarisation);
        }
    }
}

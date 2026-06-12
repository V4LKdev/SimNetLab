
#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"

namespace simnet::ecs {
    void align_heading_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_AlignHeading");

        while (it.next()) {
            auto vel = it.field<const Velocity>(0);
            auto hd = it.field<Heading>(1);

            for (int64_t i = 0; i < it.count(); i++) {
                Vec3 normal_forward;

                if (vel[i].value.length_sq() > 1e-12f) {
                    // Normal case, align heading with the normal of the velocity forward
                    normal_forward = vel[i].value.normalized();
                } else if (hd[i].value != Vec3::zero()) {
                    // Keep facing the same direction
                    continue;
                } else {
                    // Save fallback
                    normal_forward = Vec3{1, 0, 0};
                }
                hd[i].value = normal_forward;
            }
        }
    }
}

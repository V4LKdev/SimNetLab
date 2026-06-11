#include <flecs.h>

#include "telemetry.hpp"
#include "../../vec3.hpp"
#include "ecs/components.hpp"


namespace simnet::ecs {
    void clear_accumulator_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_ClearAccumulator");

        while (it.next()) {
            auto acc = it.field<SteeringAccumulate>(0);

            for (const uint64_t i: it) {
                acc[i].value = Vec3{0.0f, 0.0f, 0.0f};
            }
        }
    }
}

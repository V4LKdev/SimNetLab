#include <flecs.h>

#include "telemetry.hpp"
#include "../../../core/math/vec3.hpp"
#include "../components.hpp"


namespace simnet::ecs {
    void clear_accumulator_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_ClearAccumulator");

        while (it.next()) {
            auto acc = it.field<SteeringAccumulate>(0);

            for (size_t i: it) {
                acc[i].value = Vec3::zero();
            }
        }
    }
}

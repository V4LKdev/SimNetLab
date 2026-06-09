#pragma once
#include "ecs/components.hpp"

namespace simnet::telemetry {
    // Check that all desired velocities are within limits. Returns number of failures.
    int validate_steering_output(
        const ecs::Position *pos,
        const ecs::Velocity *vel,
        const ecs::DesiredVelocity *out,
        uint64_t count,
        const ecs::BoidConfig &cfg);
}

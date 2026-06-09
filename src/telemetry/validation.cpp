#include "validation.hpp"
#include "logger.hpp"
#include <cmath>

#include "telemetry.hpp"

namespace simnet::telemetry {
    int validate_steering_output(
        const ecs::Position * /*pos*/,
        const ecs::Velocity * /*vel*/,
        const ecs::DesiredVelocity *out,
        uint64_t count,
        const ecs::BoidConfig &cfg)
    {
        int failures = 0;
        const float eps = 0.001f;
        const float max_speed_sq = cfg.max_speed * cfg.max_speed;

        for (uint64_t i = 0; i < count; ++i) {
            const auto &dv = out[i].value;
            float len2 = dv.x * dv.x + dv.y * dv.y + dv.z * dv.z;

            if (std::isnan(dv.x) || std::isnan(dv.y) || std::isnan(dv.z) ||
                std::isinf(dv.x) || std::isinf(dv.y) || std::isinf(dv.z)) {
                TELEM_LOG_ERROR("DesiredVelocity[{}] is NaN/Inf: ({},{},{})", i, dv.x, dv.y, dv.z);
                ++failures;
                continue;
            }

            if (len2 > max_speed_sq + eps) {
                TELEM_LOG_ERROR("DesiredVelocity[{}] exceeds max_speed: len={} (max={})",
                                i, std::sqrt(len2), cfg.max_speed);
                ++failures;
            }
        }

        TELEM_LOG_INFO("Steering validation: {} failures out of {} boids", failures, count);
        return failures;
    }
}

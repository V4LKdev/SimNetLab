#pragma once
#include <chrono>

namespace simnet::config {

    constexpr int SIM_HZ = 30;
    constexpr std::chrono::nanoseconds SIM_DT = std::chrono::nanoseconds(1'000'000'000 / SIM_HZ);
    constexpr int MAX_SIM_STEPS = 5;
    constexpr int64_t MAX_ACCUM_NS = SIM_DT.count() * MAX_SIM_STEPS; // Spiral-of-death capping

    constexpr float WORLD_HALF = 5.0f;

}

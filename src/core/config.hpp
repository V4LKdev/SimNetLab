#pragma once
#include <chrono>

namespace simnet::config {

    // Simulation Rate
    constexpr int SIM_HZ = 30;
    constexpr std::chrono::nanoseconds SIM_DT = std::chrono::nanoseconds(1'000'000'000 / SIM_HZ);
    constexpr float SIM_DT_SECONDS = static_cast<float>(SIM_DT.count()) / 1'000'000'000.0f;
    constexpr int MAX_SIM_STEPS = 5;
    constexpr int64_t MAX_ACCUM_NS = SIM_DT.count() * MAX_SIM_STEPS; // Spiral-of-death capping

    // Boids
    constexpr uint32_t MAX_BOIDS = 100;
    constexpr float WORLD_HALF = 500.0f;
    constexpr float BOID_SCALE = 5.0f;

}

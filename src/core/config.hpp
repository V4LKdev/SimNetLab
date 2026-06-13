#pragma once
#include <chrono>
#include <cstdint>

/// Compile-time simulation and world constants
namespace simnet::config {
    // --- Simulation ---

    /// Simulation rate (ticks per second).
    constexpr int SIM_HZ = 30;
    /// Nominal seconds per sim tick.
    constexpr float SIM_DT_SECONDS = 1.0f / static_cast<float>(SIM_HZ);
    /// Exact nanoseconds per tick (truncated, used by accumulator)
    constexpr std::chrono::nanoseconds SIM_DT = std::chrono::nanoseconds(1'000'000'000 / SIM_HZ);
    /// Maximum simulation steps per frame to prevent spiral of death.
    constexpr int MAX_SIM_STEPS = 5;
    /// Maximum accumulated time (ns). Any excess is discarded.
    constexpr int64_t MAX_ACCUM_NS = SIM_DT.count() * MAX_SIM_STEPS;

    // --- Boids ---

    /// Maximum number of boid entities.
    constexpr uint32_t MAX_BOIDS = 200;
    /// Half-extend of the world cube.
    constexpr float WORLD_HALF = 50.0f;
    /// Visual scale of a boid entity.
    constexpr float BOID_SCALE = 1.0f;
}

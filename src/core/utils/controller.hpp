#pragma once
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <limits>

#include "config/sim_config.hpp"
#include "telemetry.hpp"

namespace simnet::core::utils {
    /**
     *  Fixed-step simulation timer / accumulator gate.
     */
    struct TimestepController {
        explicit TimestepController(const config::SimConfig &cfg) : cfg_(cfg)
        {
        }

        /**
         * Feed elapsed real time into the accumulator.
         * Caps the accumulator to max_accum_ns to prevent spiral of death.
         * Resets the per‑frame step counter.
         */
        void advance(int64_t elapsed_ns)
        {
            accumulator_ns_ += elapsed_ns;

            if (accumulator_ns_ > cfg_.max_accum_ns()) {
                TELEM_LOG_WARN("Accumulator clamped; simulation behind");
                accumulator_ns_ = cfg_.max_accum_ns();
            }

            steps_this_frame_ = 0;
        }

        /**
         * Returns true exactly once per pending fixed step.
         * Each successful call subtracts dt from the accumulator and advances sim time.
         */
        bool try_step()
        {
            if (accumulator_ns_ >= cfg_.dt_ns() && steps_this_frame_ < cfg_.max_sim_steps) {
                accumulator_ns_ -= cfg_.dt_ns();
                sim_time_ns_ += cfg_.dt_ns();
                ++steps_this_frame_;

                return true;
            }
            return false;
        }

        /** Milliseconds until the next tick is due (0 if already due). */
        [[nodiscard]] int ms_until_next_tick() const noexcept
        {
            const int64_t remaining = cfg_.dt_ns() - accumulator_ns_;
            if (remaining <= 0) return 0;

            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::nanoseconds(remaining))
                    .count();
            return static_cast<int>(std::min<int64_t>(ms, std::numeric_limits<int>::max()));
        }

        /** Alpha in [0,1) for interpolation between previous and current sim state. */
        [[nodiscard]] double interpolation_alpha() const noexcept
        {
            return static_cast<double>(accumulator_ns_) / static_cast<double>(cfg_.dt_ns());
        }

        /** Total simulated time elapsed (in nanoseconds). */
        [[nodiscard]] int64_t sim_time_ns() const noexcept { return sim_time_ns_; }

        /** Number of ticks taken this frame (since last update). */
        [[nodiscard]] int steps_this_frame() const noexcept { return steps_this_frame_; }

    private:
        const config::SimConfig &cfg_;
        int64_t accumulator_ns_ = 0;
        int64_t sim_time_ns_ = 0;
        int steps_this_frame_ = 0;
    };
} // namespace simnet::core


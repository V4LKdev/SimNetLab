#pragma once
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <limits>

#include "config/sim_config.hpp"
#include "telemetry.hpp"
#include "time_keeper.hpp"

namespace simnet::core::utils {
    /**
     *  Fixed-step simulation timer / accumulator gate.
     */
    struct TimestepController {
        explicit TimestepController(const config::SimConfig &cfg)
            : cfg_(cfg)
              , dt_(Nanoseconds(cfg.dt_ns()))
              , max_accum_(Nanoseconds(cfg.max_accum_ns()))
              , max_steps_(cfg.max_sim_steps)
        {
        }

        /**
         * Feed elapsed real time into the accumulator.
         * Caps the accumulator to max_accum_ns to prevent spiral of death.
         * Resets the per‑frame step counter.
         */
        void advance(int64_t elapsed_ns)
        {
            advance(Nanoseconds(elapsed_ns));
        }

        void advance(Nanoseconds elapsed)
        {
            accumulator_ += elapsed;

            if (accumulator_ > max_accum_) {
                TELEM_LOG_WARN("Accumulator clamped; simulation behind");
                accumulator_ = max_accum_;
            }

            steps_this_frame_ = 0;
        }

        /**
         * Returns true exactly once per pending fixed step.
         * Each successful call subtracts dt from the accumulator and advances sim time.
         */
        bool try_step()
        {
            if (accumulator_ >= dt_ && steps_this_frame_ < max_steps_) {
                accumulator_ -= dt_;
                sim_time_ += dt_;
                ++steps_this_frame_;
                return true;
            }
            return false;
        }

        /** Milliseconds until the next tick is due (0 if already due). */
        [[nodiscard]] int ms_until_next_tick() const noexcept
        {
            if (accumulator_ >= dt_) return 0;
            const auto remaining = dt_ - accumulator_;
            return to_milliseconds_saturating(remaining);
        }

        /** Alpha in [0,1) for interpolation between previous and current state. */
        [[nodiscard]] double interpolation_alpha() const noexcept
        {
            // Safe because dt_ > 0
            return static_cast<double>(accumulator_.count()) /
                   static_cast<double>(dt_.count());
        }

        /** Total simulated time elapsed. */
        [[nodiscard]] Nanoseconds sim_time() const noexcept { return sim_time_; }

        /** Total simulated time in nanoseconds (int64_t for compatibility). */
        [[nodiscard]] int64_t sim_time_ns() const noexcept
        {
            return sim_time_.count();
        }

        /** Number of ticks taken this frame. */
        [[nodiscard]] int steps_this_frame() const noexcept
        {
            return steps_this_frame_;
        }

    private:
        const config::SimConfig &cfg_;
        Nanoseconds dt_;
        Nanoseconds max_accum_;
        int max_steps_;

        Nanoseconds accumulator_{};
        Nanoseconds sim_time_{};
        int steps_this_frame_ = 0;
    };
}


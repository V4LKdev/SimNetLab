#pragma once
#include <chrono>

#include "config/SimConfig.hpp"
#include "simulation.hpp"
#include "telemetry.hpp"


namespace simnet::core {
    using Clock = std::chrono::steady_clock;

    /*
     *  Fixed-step simulation timer with accumulator.
     *  Prevents spiral of death by capping the number of sim steps per frame.
     */
    struct TimestepController {
        /// Constructs and captures current time as last_time_
        explicit TimestepController(sim::Simulation &sim, const SimConfig &cfg)
            : sim_(sim), cfg_(cfg), last_time_(Clock::now())
        {
        }

        /*
         *  Advance simulation by as many fixed steps as accumulated real time allows.
         *  \returns number of simulation steps executed this frame.
         */
        int update()
        {
            const std::chrono::time_point now = Clock::now();
            const std::chrono::nanoseconds elapsed = now - last_time_;
            last_time_ = now;

            accumulator_ns_ += elapsed.count();

            if (accumulator_ns_ >= cfg_.max_accum_ns()) {
                TELEM_LOG_WARN("Accumulator clamped; simulation behind");
                accumulator_ns_ = cfg_.max_accum_ns();
            }

            steps_this_frame_ = 0;
            while (accumulator_ns_ >= cfg_.dt_ns() &&
                   steps_this_frame_ < cfg_.max_sim_steps) {
                sim_time_ns_ += cfg_.dt_ns();
                accumulator_ns_ -= cfg_.dt_ns();

                sim_.step();

                ++steps_this_frame_;
                TELEM_TRACY_PLOT("StepsThisFrame", static_cast<int64_t>(steps_this_frame_));
            }
            return steps_this_frame_;
        }

        /// Interpolation factor [0, 1) between previous and current sim state.
        [[nodiscard]]
        double get_interpolation_alpha() const
        {
            return static_cast<double>(accumulator_ns_) / static_cast<double>(cfg_.dt_ns());
        }

        /// Total elapsed simulation time in nanoseconds
        [[nodiscard]]
        int64_t get_sim_elapsed_ns() const
        {
            return sim_time_ns_;
        }

        [[nodiscard]]
        int ms_until_next_tick() const noexcept
        {
            const auto remaining = cfg_.dt_ns() - accumulator_ns_;
            if (remaining <= 0) return 0;

            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::nanoseconds(remaining)).
                    count();

            return static_cast<int>(std::min<long long>(ms, std::numeric_limits<int>::max()));
        }

    private:
        sim::Simulation &sim_;
        const SimConfig &cfg_;
        Clock::time_point last_time_;
        int64_t accumulator_ns_ = 0;
        int64_t sim_time_ns_ = 0;
        int steps_this_frame_ = 0;
    };
}

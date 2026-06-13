#pragma once
#include <chrono>

#include "config.hpp"
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
        explicit TimestepController(sim::Simulation &sim)
            : sim_(sim), last_time_(Clock::now())
        {
        }

        /*
         *  Advance simulation by as many fixed steps as accumulated real time allows.
         *  \returns number of simulation steps executed this frame.
         */
        int update()
        {
            TELEM_TRACY_ZONE("ControllerUpdate");

            const std::chrono::time_point now = Clock::now();
            const std::chrono::nanoseconds elapsed = now - last_time_;
            last_time_ = now;

            accumulator_ns_ += elapsed.count();

            if (accumulator_ns_ >= config::MAX_ACCUM_NS) {
                TELEM_LOG_WARN("Accumulator clamped; simulation behind");
                accumulator_ns_ = config::MAX_ACCUM_NS;
            }

            steps_this_frame_ = 0;
            while (accumulator_ns_ >= config::SIM_DT.count() &&
                   steps_this_frame_ < config::MAX_SIM_STEPS) {
                sim_time_ns_ += config::SIM_DT.count();
                accumulator_ns_ -= config::SIM_DT.count();

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
            return static_cast<double>(accumulator_ns_) / static_cast<double>(config::SIM_DT.count());
        }

        /// Total elapsed simulation time in nanoseconds
        [[nodiscard]]
        int64_t get_sim_elapsed_ns() const
        {
            return sim_time_ns_;
        }

    private:
        sim::Simulation &sim_;
        Clock::time_point last_time_;
        int64_t accumulator_ns_ = 0;
        int64_t sim_time_ns_ = 0;
        int steps_this_frame_ = 0;
    };
}

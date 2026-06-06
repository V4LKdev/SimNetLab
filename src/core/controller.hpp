#pragma once
#include <chrono>

#include "config.hpp"
#include "simulation.hpp"

namespace simnet::core {

    using Clock = std::chrono::steady_clock;

    struct TimestepController {

        explicit TimestepController(sim::Simulation& sim)
            : sim_(sim), last_time_(Clock::now()) {}

        int update() {
            const std::chrono::time_point now = Clock::now();
            const std::chrono::nanoseconds elapsed = now - last_time_;
            last_time_ = now;

            accumulator_ns_ += elapsed.count();
            accumulator_ns_ = std::min(accumulator_ns_, config::MAX_ACCUM_NS);

            steps_this_frame_ = 0;
            while (accumulator_ns_ >= config::SIM_DT.count() &&
                    steps_this_frame_ < config::MAX_SIM_STEPS)
            {
                sim_time_ns_ += config::SIM_DT.count();
                accumulator_ns_ -= config::SIM_DT.count();

                sim_.step();

                ++steps_this_frame_;
            }
            return steps_this_frame_;
        }

        [[nodiscard]]
        double get_interpolation_alpha() const {
            return static_cast<double>(accumulator_ns_) / static_cast<double>(config::SIM_DT.count());
        }

    private:
        sim::Simulation& sim_;
        Clock::time_point last_time_;
        int64_t accumulator_ns_ = 0;
        int64_t sim_time_ns_ = 0;
        int steps_this_frame_ = 0;
    };

}

/// \file   timestep.cpp
/// \brief  TimestepController implementation.

module;

#include <cstdint>
#include <chrono>
module simnet.utils;

namespace simnet::utils {
    using Nanoseconds = std::chrono::nanoseconds;

    TimestepController::TimestepController(TimestepSettings settings)
        : dt_(Nanoseconds(settings.dt_ns())),
          max_accum_(Nanoseconds(settings.max_accum_ns())),
          max_steps_(settings.normalized_max_sim_steps())
    {
    }

    void TimestepController::advance(int64_t elapsed_ns)
    {
        advance(Nanoseconds(elapsed_ns));
    }

    void TimestepController::advance(Nanoseconds elapsed)
    {
        accumulator_ += elapsed;
        if (accumulator_ > max_accum_) {
            accumulator_ = max_accum_;
        }
        steps_this_frame_ = 0;
    }

    bool TimestepController::try_step()
    {
        if (accumulator_ >= dt_ && steps_this_frame_ < max_steps_) {
            accumulator_ -= dt_;
            sim_time_ += dt_;
            ++steps_this_frame_;
            return true;
        }
        return false;
    }

    int TimestepController::ms_until_next_tick() const noexcept
    {
        if (accumulator_ >= dt_) return 0;
        const auto remaining = dt_ - accumulator_;
        return to_milliseconds_saturating(remaining);
    }

    double TimestepController::interpolation_alpha() const noexcept
    {
        return static_cast<double>(accumulator_.count()) /
               static_cast<double>(dt_.count());
    }
}

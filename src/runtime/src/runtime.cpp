module;

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

module simnet.runtime;

import :plan;
import :types;
import simnet.core;

namespace simnet
{
    Nanoseconds steady_now_ns() noexcept
    {
        return std::chrono::duration_cast<Nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        );
    }

    void reset_frame_timer(RuntimeFrameTimer& timer, Nanoseconds now) noexcept
    {
        timer.previous = now;
        timer.initialized = true;
    }

    void reset_frame_timer(RuntimeFrameTimer& timer) noexcept
    {
        reset_frame_timer(timer, steady_now_ns());
    }

    Nanoseconds sample_frame_delta(RuntimeFrameTimer& timer, Nanoseconds now) noexcept
    {
        if (!timer.initialized)
        {
            reset_frame_timer(timer, now);
            return Nanoseconds{};
        }

        auto const previous = timer.previous;
        timer.previous = now;
        if (now <= previous)
        {
            return Nanoseconds{};
        }
        return now - previous;
    }

    Nanoseconds sample_frame_delta(RuntimeFrameTimer& timer) noexcept
    {
        return sample_frame_delta(timer, steady_now_ns());
    }

    RuntimeFramePlan plan_runtime_frame(
        FixedStepClock& clock,
        RuntimeStats& stats,
        Nanoseconds raw_delta,
        RuntimeSettings const& settings
    ) noexcept
    {
        auto plan = RuntimeFramePlan{
            .frame = stats.frames,
            .first_tick = clock.tick + 1U,
            .raw_delta = raw_delta,
        };

        auto accepted_delta = std::max(raw_delta, Nanoseconds{});
        if (accepted_delta > settings.max_frame_time)
        {
            plan.frame_delta_clamped = true;
            plan.clamped_time = accepted_delta - settings.max_frame_time;
            accepted_delta = settings.max_frame_time;
        }
        plan.accepted_delta = accepted_delta;

        auto step_settings = settings.fixed_step;
        if (settings.max_ticks != 0U)
        {
            auto const remaining_ticks =
                clock.tick < settings.max_ticks ? settings.max_ticks - clock.tick : Tick{};
            auto const bounded_remaining =
                std::min<Tick>(remaining_ticks, std::numeric_limits<std::uint16_t>::max());
            step_settings.max_steps_per_frame = std::min(
                step_settings.max_steps_per_frame,
                static_cast<std::uint16_t>(bounded_remaining)
            );
        }

        plan.step_count = advance(clock, accepted_delta, step_settings);
        if (clock.fixed_dt > Nanoseconds{} && clock.accumulator >= clock.fixed_dt)
        {
            plan.step_limit_reached = true;
            auto const retained_remainder = clock.accumulator % clock.fixed_dt;
            plan.dropped_time = clock.accumulator - retained_remainder;
            clock.accumulator = retained_remainder;
        }

        plan.accumulator = clock.accumulator;
        if (clock.fixed_dt > Nanoseconds{})
        {
            plan.interpolation_alpha = static_cast<double>(clock.accumulator.count()) /
                                       static_cast<double>(clock.fixed_dt.count());
        }

        ++stats.frames;
        stats.ticks = clock.tick;
        stats.capped_frames += plan.step_limit_reached ? 1U : 0U;
        stats.raw_time += std::max(raw_delta, Nanoseconds{});
        stats.accepted_time += plan.accepted_delta;
        stats.clamped_time += plan.clamped_time;
        stats.dropped_time += plan.dropped_time;
        return plan;
    }

    ShutdownReason
    reached_runtime_limit(RuntimeSettings const& settings, RuntimeStats const& stats) noexcept
    {
        if (settings.max_ticks != 0U && stats.ticks >= settings.max_ticks)
        {
            return ShutdownReason::TickLimit;
        }
        if (settings.max_frames != 0U && stats.frames >= settings.max_frames)
        {
            return ShutdownReason::FrameLimit;
        }
        if (settings.max_runtime > Nanoseconds{} && stats.raw_time >= settings.max_runtime)
        {
            return ShutdownReason::RuntimeLimit;
        }
        return ShutdownReason::None;
    }
}

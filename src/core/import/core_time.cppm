module;

#include <chrono>
#include <cstdint>

/// @brief Core fixed-step timing types.
export module simnet.core:time;

export namespace simnet
{
    /// Monotonic simulation tick index.
    using Tick = std::uint64_t;

    /// Nanosecond alias.
    using NS = std::chrono::nanoseconds;

    /// Fixed-step simulation configuration.
    struct FixedStepSettings
    {
        /// Tick rate in hertz.
        double tick_rate_hz{ 60.0 };

        /// Ceiling applied to incoming frame deltas.
        NS max_frame_time{ std::chrono::milliseconds(250) };

        /// Hard limit on the number of ticks consumed in one frame.
        std::uint16_t max_steps_per_frame{ 5 };
    };

    /// Accumulator state for a fixed-step clock.
    struct FixedStepClock
    {
        /// Fractional wall-time not yet consumed by a tick.
        NS    accumulator{};
        /// Duration of one tick, derived from tick_rate_hz.
        NS    fixed_dt{};
        /// Current tick index (monotonic).
        Tick  tick{};
    };

     /// Computes the duration of one fixed step for the given tick rate.
     /// @return The fixed timestep, or zero if `tick_rate_hz <= 0`.
    [[nodiscard]] constexpr NS fixed_dt_from_tick_rate(
        const FixedStepSettings &settings) noexcept
    {
        if (settings.tick_rate_hz <= 0.0) {
            return NS{ 0 };
        }

        const double seconds = 1.0 / settings.tick_rate_hz;

        // Round to nearest nanosecond
        return std::chrono::round<NS>(
            std::chrono::duration<double>(seconds)
        );
    }


     /// Creates a FixedStepClock pre-initialised with the correct fixed_dt.
     /// `accumulator` and `tick` start at zero.
    [[nodiscard]] constexpr FixedStepClock make_clock(
        const FixedStepSettings &settings) noexcept
    {
        return FixedStepClock{
            .fixed_dt = fixed_dt_from_tick_rate(settings),
        };
    }


     /// Advances the clock by one frame of incoming time.
     /// @return Number of ticks performed this frame.
    [[nodiscard]] constexpr std::uint16_t advance(
        FixedStepClock       &state,
        NS                    delta,
        const FixedStepSettings &settings) noexcept
    {
        // Refuse to run with a non-positive timestep
        if (state.fixed_dt <= NS{ 0 }) {
            return 0;
        }

        // Clamp incoming delta to avoid a death spiral
        if (delta > settings.max_frame_time) {
            delta = settings.max_frame_time;
        }

        state.accumulator += delta;

        std::uint16_t steps_this_frame = 0;

        // Consume whole ticks, respecting the per-frame cap
        while (state.accumulator >= state.fixed_dt &&
               steps_this_frame < settings.max_steps_per_frame)
        {
            state.accumulator -= state.fixed_dt;
            ++state.tick;
            ++steps_this_frame;
        }

        return steps_this_frame;
    }
}


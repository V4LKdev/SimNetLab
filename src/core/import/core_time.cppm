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
    using Nanoseconds = std::chrono::nanoseconds;

    /// Fixed-step simulation configuration.
    struct FixedStepSettings
    {
        /// Tick rate in hertz.
        double tick_rate_hz{60.0};

        /// Hard limit on the number of ticks consumed in one frame.
        std::uint16_t max_steps_per_frame{5};
    };

    /// Accumulator state for a fixed-step clock.
    struct FixedStepClock
    {
        /// Fractional wall-time not yet consumed by a tick.
        Nanoseconds accumulator{};
        /// Duration of one tick, derived from tick_rate_hz.
        Nanoseconds fixed_dt{};
        /// Current tick index (monotonic).
        Tick tick{};
    };

    /// Creates a FixedStepClock pre-initialized with the correct fixed_dt.
    /// `accumulator` and `tick` start at zero.
    [[nodiscard]] constexpr FixedStepClock make_clock(const FixedStepSettings& settings) noexcept
    {
        if (settings.tick_rate_hz <= 0.0)
        {
            return {};
        }

        const double seconds = 1.0 / settings.tick_rate_hz;
        return FixedStepClock{
            .fixed_dt = std::chrono::round<Nanoseconds>(std::chrono::duration<double>(seconds)),
        };
    }

    /// Advances the clock by an already accepted nonnegative duration.
    /// The caller owns elapsed-time acceptance policy.
    /// @return Number of ticks performed this frame.
    [[nodiscard]] constexpr std::uint16_t
    advance(FixedStepClock& state, Nanoseconds delta, const FixedStepSettings& settings) noexcept
    {
        // Refuse to run with a non-positive timestep
        if (state.fixed_dt <= Nanoseconds{0})
        {
            return 0;
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

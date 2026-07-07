module;

#include <chrono>
#include <cstdint>

/// @brief Core fixed-step timing types.
export module simnet.core:time;

export namespace simnet
{
    /// Monotonic simulation tick index.
    using Tick = std::uint64_t;

    /// Fixed-step simulation timing settings.
    struct FixedStepSettings
    {
        double tick_rate_hz { 60.0 };
    };

    /// Mutable fixed-step accumulator state.
    struct FixedStepClock
    {
        std::chrono::nanoseconds accumulator {};
        std::chrono::nanoseconds fixed_dt {};
        Tick tick {};
    };

    /// Returns the fixed timestep duration for the configured tick rate.
    [[nodiscard]] constexpr std::chrono::nanoseconds fixed_dt_from_tick_rate(
        FixedStepSettings settings
    ) noexcept
    {
        if (settings.tick_rate_hz <= 0.0) {
            return std::chrono::nanoseconds { 0 };
        }

        const auto seconds = 1.0 / settings.tick_rate_hz;
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(seconds)
        );
    }
}

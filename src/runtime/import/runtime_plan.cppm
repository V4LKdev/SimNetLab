module;

/// @brief Runtime frame timing and fixed-step planning helpers.
export module simnet.runtime:plan;

import :types;

export namespace simnet
{
    /// Returns the current steady-clock epoch duration rounded to nanoseconds.
    [[nodiscard]] Nanoseconds steady_now_ns() noexcept;

    /// Resets a frame timer to an explicit monotonic nanosecond timestamp.
    void reset_frame_timer(RuntimeFrameTimer& timer, Nanoseconds now) noexcept;

    /// Resets a frame timer to the current steady-clock time.
    void reset_frame_timer(RuntimeFrameTimer& timer) noexcept;

    /// Samples a non-negative delta from an explicit monotonic timestamp.
    [[nodiscard]] Nanoseconds
    sample_frame_delta(RuntimeFrameTimer& timer, Nanoseconds now) noexcept;

    /// Samples a nanosecond frame delta from std::chrono::steady_clock.
    [[nodiscard]] Nanoseconds sample_frame_delta(RuntimeFrameTimer& timer) noexcept;

    /**
     * Plans one outer frame and advances the supplied fixed-step clock.
     *
     * Raw elapsed time is accepted as a nonnegative duration capped by
     * RuntimeSettings::max_frame_time. Rejected excess is reported as
     * clamped_time.
     *
     * Whole-step backlog remaining after the step limit is discarded. The
     * fractional remainder is retained for interpolation and the discarded
     * duration is reported as dropped_time.
     */
    [[nodiscard]] RuntimeFramePlan plan_runtime_frame(
        FixedStepClock& clock,
        RuntimeStats& stats,
        Nanoseconds raw_delta,
        RuntimeSettings const& settings
    ) noexcept;

    /// Returns the first configured runtime limit reached, or None.
    [[nodiscard]] ShutdownReason
    reached_runtime_limit(RuntimeSettings const& settings, RuntimeStats const& stats) noexcept;
}

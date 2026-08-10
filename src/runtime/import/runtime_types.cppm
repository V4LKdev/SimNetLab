module;

#include <cstdint>

/// @brief Runtime settings, reports, counters, and shutdown state.
export module simnet.runtime:types;

import simnet.core;

export namespace simnet
{
    /// Why an application runtime stopped or intends to stop.
    enum class ShutdownReason : std::uint8_t
    {
        None,
        Requested,
        Signal,
        FrameLimit,
        TickLimit,
        RuntimeLimit,
        WindowClosed,
        TransportDisconnected,
        FatalError
    };

    /// Generic runtime policy. A zero limit is disabled.
    struct RuntimeSettings
    {
        FixedStepSettings fixed_step{};
        Nanoseconds max_frame_time{250'000'000};
        std::uint64_t max_frames{};
        Tick max_ticks{};
        Nanoseconds max_runtime{};
    };

    /// Result of planning one outer runtime frame.
    struct RuntimeFramePlan
    {
        std::uint64_t frame{};
        Tick first_tick{};
        std::uint16_t step_count{};
        Nanoseconds raw_delta{};
        Nanoseconds accepted_delta{};
        Nanoseconds clamped_time{};
        Nanoseconds dropped_time{};
        Nanoseconds accumulator{};
        double interpolation_alpha{};
        bool frame_delta_clamped{};
        bool step_limit_reached{};
    };

    /// Cumulative generic runtime counters.
    struct RuntimeStats
    {
        std::uint64_t frames{};
        Tick ticks{};
        std::uint64_t capped_frames{};
        Nanoseconds raw_time{};
        Nanoseconds accepted_time{};
        Nanoseconds clamped_time{};
        Nanoseconds dropped_time{};
    };

    /// First-wins stop request owned by runtime control code.
    class StopRequest
    {
      public:
        /// Requests shutdown. Returns true only for the first accepted reason.
        [[nodiscard]] bool request(ShutdownReason reason = ShutdownReason::Requested) noexcept
        {
            if (reason == ShutdownReason::None)
            {
                return false;
            }
            if (reason_ != ShutdownReason::None)
            {
                return false;
            }
            reason_ = reason;
            return true;
        }

        [[nodiscard]] bool requested() const noexcept
        {
            return reason() != ShutdownReason::None;
        }

        [[nodiscard]] ShutdownReason reason() const noexcept
        {
            return reason_;
        }

      private:
        ShutdownReason reason_{ShutdownReason::None};
    };

    /// State used to sample monotonic nanosecond frame deltas.
    struct RuntimeFrameTimer
    {
        Nanoseconds previous{};
        bool initialized{};
    };
}

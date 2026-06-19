#pragma once

#include <chrono>
#include <limits>
#include <type_traits>

namespace simnet::core::utils {
    //  --- Convenience aliases ---
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;
    using Nanoseconds = std::chrono::nanoseconds;
    using Microseconds = std::chrono::microseconds;
    using Milliseconds = std::chrono::milliseconds;
    using Seconds = std::chrono::seconds;

    // --- Safe duration casts ---
    template<typename ToDur, typename Rep, typename Period>
    [[nodiscard]] constexpr ToDur duration_cast_safe(
        const std::chrono::duration<Rep, Period> &d)
    {
        const auto src_count = d.count();
        constexpr auto src_period = std::chrono::duration<Rep, Period>::period::den /
                                    std::chrono::duration<Rep, Period>::period::num;
        constexpr auto dst_period = ToDur::period::den / ToDur::period::num;

        return std::chrono::duration_cast<ToDur>(d);
    }

    // --- Comparisons with tolerance (useful for tests and near‑equal checks) ---
    template<typename Rep, typename Period>
    [[nodiscard]] constexpr bool duration_approx_equal(
        const std::chrono::duration<Rep, Period> &a,
        const std::chrono::duration<Rep, Period> &b,
        const std::chrono::duration<Rep, Period> &epsilon = Nanoseconds(1))
    {
        const auto diff = a > b ? a - b : b - a;
        return diff <= epsilon;
    }

    // --- Convenience: convert to milliseconds (int) with saturating clamp ---
    template<typename Rep, typename Period>
    [[nodiscard]] constexpr int to_milliseconds_saturating(
        const std::chrono::duration<Rep, Period> &d)
    {
        const auto ms = std::chrono::duration_cast<Milliseconds>(d);
        if (ms.count() > std::numeric_limits<int>::max())
            return std::numeric_limits<int>::max();
        if (ms.count() < std::numeric_limits<int>::min())
            return std::numeric_limits<int>::min();
        return static_cast<int>(ms.count());
    }

    //  --- Frame timer: measure elapsed time since last reset ---
    class FrameTimer {
    public:
        void reset() noexcept { start_ = Clock::now(); }

        [[nodiscard]] Duration elapsed() const noexcept
        {
            return Clock::now() - start_;
        }

        [[nodiscard]] Nanoseconds elapsed_ns() const noexcept
        {
            return std::chrono::duration_cast<Nanoseconds>(elapsed());
        }

    private:
        TimePoint start_ = Clock::now();
    };
}

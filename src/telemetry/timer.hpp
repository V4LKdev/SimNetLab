#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace simnet::telemetry {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    // A named timer that accumulates statistics over many invocations.
    struct TimerEntry {
        std::string name;
        Duration min = Duration::max();
        Duration max = Duration::zero();
        Duration total = Duration::zero();
        uint64_t count = 0;
    };

    // Registry of all timers
    class TimerRegistry {
    public:
        static TimerRegistry &instance();

        void record(std::string_view name, Duration elapsed);

        std::vector<TimerEntry> entries() const;

        void reset();

    private:
        mutable std::mutex mutex_;
        std::vector<TimerEntry> entries_;
    };

    // RAII scoped timer
    class ScopedTimer {
    public:
        explicit ScopedTimer(std::string_view name);

        ~ScopedTimer();

        ScopedTimer(const ScopedTimer &) = delete;

        ScopedTimer &operator=(const ScopedTimer &) = delete;

    private:
        std::string_view name_;
        TimePoint start_;
    };


    enum class FrameEvent { Begin, End };

    // Called by TELEM_FRAME_BEGIN/END
    void frame_marker(FrameEvent event);

    // Access per‑frame timing (in nanoseconds)
    Duration last_frame_time();

    double last_frame_time_ms();

    struct FrameStats {
        double avg_ms = 0.0;
        uint64_t frame_count = 0;
    };

    FrameStats get_frame_stats();

    void dump_summary();
} // namespace simnet::telemetry

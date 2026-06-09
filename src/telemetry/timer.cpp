#include "timer.hpp"

#include <spdlog/logger.h>
#include "logger.hpp"

namespace simnet::telemetry {
    TimerRegistry &TimerRegistry::instance()
    {
        static TimerRegistry reg;
        return reg;
    }

    void TimerRegistry::record(std::string_view name, Duration elapsed)
    {
        std::lock_guard lock(mutex_);

        auto it = std::find_if(entries_.begin(), entries_.end(), [&](const TimerEntry &e) { return e.name == name; });
        if (it == entries_.end()) {
            entries_.push_back({std::string(name)});
            it = entries_.end() - 1;
        }
        it->min = std::min(it->min, elapsed);
        it->max = std::max(it->max, elapsed);
        it->total += elapsed;
        ++it->count;
    }

    std::vector<TimerEntry> TimerRegistry::entries() const
    {
        std::lock_guard lock(mutex_);
        return entries_;
    }

    void TimerRegistry::reset()
    {
        std::lock_guard lock(mutex_);
        entries_.clear();
    }

    // ScopedTimer

    ScopedTimer::ScopedTimer(std::string_view name)
        : name_(name),
          start_(Clock::now())
    {
    }

    ScopedTimer::~ScopedTimer()
    {
        auto end = Clock::now();
        TimerRegistry::instance().record(name_, end - start_);
    }

    // Frame markers

    namespace {
        std::mutex frame_mutex_;
        TimePoint last_frame_start_;
        TimePoint last_frame_end_;
        Duration last_frame_dur{0};

        struct FrameAccumulator {
            double total_ms = 0.0;
            uint64_t count = 0;
        };

        FrameAccumulator g_frame_acc;
    }

    void frame_marker(FrameEvent event)
    {
        std::lock_guard lock(frame_mutex_);

        switch (event) {
            case FrameEvent::Begin:
                last_frame_start_ = Clock::now();
                break;
            case FrameEvent::End:
                last_frame_end_ = Clock::now();
                last_frame_dur = last_frame_end_ - last_frame_start_;
                // Accumulate
                double ms = std::chrono::duration<double, std::milli>(last_frame_dur).count();
                g_frame_acc.total_ms += ms;
                g_frame_acc.count++;
                break;
        }
    }

    Duration last_frame_time()
    {
        std::lock_guard lock(frame_mutex_);
        return last_frame_dur;
    }

    double last_frame_time_ms()
    {
        return std::chrono::duration<double, std::milli>(last_frame_time()).count();
    }

    FrameStats get_frame_stats()
    {
        std::lock_guard lock(frame_mutex_);
        FrameStats s;
        if (g_frame_acc.count > 0) {
            s.avg_ms = g_frame_acc.total_ms / g_frame_acc.count;
        }
        s.frame_count = g_frame_acc.count;
        return s;
    }

    void dump_summary()
    {
        auto logger = get_logger();
        if (!logger) return;

        // Frame stats
        auto fs = get_frame_stats();
        logger->info("=== Telemetry Summary ===");
        logger->info("Frames    : {} (avg {:.3f} ms)", fs.frame_count, fs.avg_ms);

        // Timer entries
        auto entries = TimerRegistry::instance().entries();
        if (entries.empty()) {
            logger->info("No timers recorded.");
            return;
        }

        // Sort by total time descending for readability
        std::sort(entries.begin(), entries.end(), [](const TimerEntry &a, const TimerEntry &b) {
            return a.total > b.total;
        });

        logger->info("Timers (sorted by total):");
        for (auto &e: entries) {
            double min_ms = std::chrono::duration<double, std::milli>(e.min).count();
            double max_ms = std::chrono::duration<double, std::milli>(e.max).count();
            double avg_ms = e.count > 0
                                ? std::chrono::duration<double, std::milli>(e.total).count() / e.count
                                : 0.0;
            logger->info("  {:<30} min={:8.3f} | max={:8.3f} | avg={:8.3f} ms | ({} calls)",
                         e.name, min_ms, max_ms, avg_ms, e.count);
        }
        logger->info("=========================");
    }
}

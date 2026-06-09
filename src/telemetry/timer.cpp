#include "timer.hpp"

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
}

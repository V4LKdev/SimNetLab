#include <catch2/catch_test_macros.hpp>
#include <chrono>

import simnet.core;
import simnet.runtime;

TEST_CASE("runtime frame plans report clamp, overload, counters, and limits", "[runtime]")
{
    using namespace std::chrono_literals;

    auto const settings = simnet::RuntimeSettings {
        .fixed_step = {
            .tick_rate_hz = 10.0,
            .max_frame_time = 1s,
            .max_steps_per_frame = 2,
        },
        .max_frames = 2,
        .max_ticks = 3,
    };
    auto clock = simnet::make_clock(settings.fixed_step);
    auto stats = simnet::RuntimeStats{};

    auto const overloaded = simnet::plan_runtime_frame(clock, stats, 850ms, settings);
    CHECK(overloaded.frame == 0);
    CHECK(overloaded.first_tick == 1);
    CHECK(overloaded.step_count == 2);
    CHECK(overloaded.step_limit_reached);
    CHECK(overloaded.dropped_time == 600ms);
    CHECK(overloaded.accumulator == 50ms);
    CHECK(overloaded.interpolation_alpha == 0.5);
    CHECK(stats.frames == 1);
    CHECK(stats.ticks == 2);
    CHECK(stats.capped_frames == 1);

    auto const limited = simnet::plan_runtime_frame(clock, stats, 250ms, settings);
    CHECK(limited.first_tick == 3);
    CHECK(limited.step_count == 1);
    CHECK(clock.tick == 3);
    CHECK(simnet::reached_runtime_limit(settings, stats) == simnet::ShutdownReason::TickLimit);
}

TEST_CASE("runtime stop requests preserve the first shutdown reason", "[runtime]")
{
    auto stop = simnet::StopRequest{};

    CHECK_FALSE(stop.requested());
    CHECK(stop.request(simnet::ShutdownReason::Signal));
    CHECK_FALSE(stop.request(simnet::ShutdownReason::FatalError));
    CHECK(stop.requested());
    CHECK(stop.reason() == simnet::ShutdownReason::Signal);
}

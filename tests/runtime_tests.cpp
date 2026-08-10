#include <catch2/catch_test_macros.hpp>
#include <chrono>

import simnet.core;
import simnet.runtime;

TEST_CASE("runtime frame plans own frame delta acceptance", "[runtime]")
{
    using namespace std::chrono_literals;

    auto const settings = simnet::RuntimeSettings{
        .fixed_step =
            {
                .tick_rate_hz = 10.0,
                .max_steps_per_frame = 5,
            },
        .max_frame_time = 250ms,
    };

    SECTION("duration below maximum is accepted unchanged")
    {
        auto clock = simnet::make_clock(settings.fixed_step);
        auto stats = simnet::RuntimeStats{};

        auto const plan = simnet::plan_runtime_frame(clock, stats, 240ms, settings);

        CHECK(plan.raw_delta == 240ms);
        CHECK(plan.accepted_delta == 240ms);
        CHECK(plan.clamped_time == 0ns);
        CHECK_FALSE(plan.frame_delta_clamped);
        CHECK(plan.dropped_time == 0ns);
        CHECK(stats.raw_time == 240ms);
        CHECK(stats.accepted_time == 240ms);
        CHECK(stats.clamped_time == 0ns);
        CHECK(stats.dropped_time == 0ns);
    }

    SECTION("duration at maximum is accepted unchanged")
    {
        auto clock = simnet::make_clock(settings.fixed_step);
        auto stats = simnet::RuntimeStats{};

        auto const plan = simnet::plan_runtime_frame(clock, stats, 250ms, settings);

        CHECK(plan.accepted_delta == 250ms);
        CHECK(plan.clamped_time == 0ns);
        CHECK_FALSE(plan.frame_delta_clamped);
        CHECK(plan.dropped_time == 0ns);
    }

    SECTION("duration above maximum is clamped once")
    {
        auto clock = simnet::make_clock(settings.fixed_step);
        auto stats = simnet::RuntimeStats{};

        auto const plan = simnet::plan_runtime_frame(clock, stats, 400ms, settings);

        CHECK(plan.raw_delta == 400ms);
        CHECK(plan.accepted_delta == 250ms);
        CHECK(plan.clamped_time == 150ms);
        CHECK(plan.frame_delta_clamped);
        CHECK(plan.step_count == 2);
        CHECK(plan.accumulator == 50ms);
        CHECK(plan.dropped_time == 0ns);
        CHECK(stats.raw_time == 400ms);
        CHECK(stats.accepted_time == 250ms);
        CHECK(stats.clamped_time == 150ms);
        CHECK(stats.dropped_time == 0ns);
        CHECK(stats.raw_time == stats.accepted_time + stats.clamped_time);
    }
}

TEST_CASE("runtime frame plans cap catch-up work independently", "[runtime]")
{
    using namespace std::chrono_literals;

    auto const settings = simnet::RuntimeSettings{
        .fixed_step =
            {
                .tick_rate_hz = 10.0,
                .max_steps_per_frame = 2,
            },
        .max_frame_time = 1s,
        .max_frames = 2,
        .max_ticks = 3,
    };
    auto clock = simnet::make_clock(settings.fixed_step);
    auto stats = simnet::RuntimeStats{};

    auto const overloaded = simnet::plan_runtime_frame(clock, stats, 550ms, settings);
    CHECK(overloaded.frame == 0);
    CHECK(overloaded.first_tick == 1);
    CHECK(overloaded.accepted_delta == 550ms);
    CHECK(overloaded.clamped_time == 0ns);
    CHECK_FALSE(overloaded.frame_delta_clamped);
    CHECK(overloaded.step_count == 2);
    CHECK(overloaded.step_limit_reached);
    CHECK(overloaded.dropped_time == 300ms);
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

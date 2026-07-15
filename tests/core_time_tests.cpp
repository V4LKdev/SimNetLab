#include <catch2/catch_test_macros.hpp>
#include <chrono>

import simnet.core;

TEST_CASE("fixed-step planning clamps frame time and caps catch-up work", "[core][runtime]")
{
    using namespace std::chrono_literals;

    auto const settings = simnet::FixedStepSettings {
        .tick_rate_hz = 10.0,
        .max_frame_time = 250ms,
        .max_steps_per_frame = 2,
    };
    auto clock = simnet::make_clock(settings);

    REQUIRE(clock.fixed_dt == 100ms);
    REQUIRE(simnet::advance(clock, 1s, settings) == 2);
    REQUIRE(clock.tick == 2);
    REQUIRE(clock.accumulator == 50ms);

    REQUIRE(simnet::advance(clock, 50ms, settings) == 1);
    REQUIRE(clock.tick == 3);
    REQUIRE(clock.accumulator == 0ns);
}

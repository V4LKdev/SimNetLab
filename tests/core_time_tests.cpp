#include <catch2/catch_test_macros.hpp>
#include <chrono>

import simnet.core;

TEST_CASE("fixed-step advancement consumes accepted duration and caps work", "[core][runtime]")
{
    using namespace std::chrono_literals;

    auto const settings = simnet::FixedStepSettings{
        .tick_rate_hz = 10.0,
        .max_steps_per_frame = 2,
    };
    auto clock = simnet::make_clock(settings);

    REQUIRE(clock.fixed_dt == 100ms);
    REQUIRE(simnet::advance(clock, 1s, settings) == 2);
    REQUIRE(clock.tick == 2);
    REQUIRE(clock.accumulator == 800ms);

    auto ordinary_clock = simnet::make_clock(settings);
    REQUIRE(simnet::advance(ordinary_clock, 50ms, settings) == 0);
    REQUIRE(ordinary_clock.tick == 0);
    REQUIRE(ordinary_clock.accumulator == 50ms);
    REQUIRE(simnet::advance(ordinary_clock, 50ms, settings) == 1);
    REQUIRE(ordinary_clock.tick == 1);
    REQUIRE(ordinary_clock.accumulator == 0ns);
}

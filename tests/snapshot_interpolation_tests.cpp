#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <vector>

import simnet.core;
import simnet.snapshot;

namespace
{
    [[nodiscard]] simnet::WorldSnapshot
    snapshot(simnet::Tick tick, std::initializer_list<simnet::EntityState> boids)
    {
        auto result = simnet::WorldSnapshot{};
        result.tick = tick;
        result.reserve(boids.size());
        for (auto const& boid : boids) {
            result.ids.push_back(boid.id);
            result.positions.push_back(boid.position);
            result.headings.push_back(boid.heading);
            result.hues.push_back(boid.hue);
        }
        return result;
    }
}

TEST_CASE("snapshot interpolation blends matching presentation state", "[snapshot][interpolation]")
{
    auto const previous = snapshot(
        10U,
        {
            {1U, {}, {1.0F, 0.0F, 0.0F}, 250U},
        }
    );
    auto const current = snapshot(
        11U,
        {
            {1U, {10.0F, 4.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 6U},
        }
    );
    auto output = simnet::WorldSnapshot{};

    REQUIRE(simnet::interpolate_world_snapshots(previous, current, 0.5, output).valid);
    REQUIRE(output.size() == 1U);
    CHECK(output.tick == 11U);
    CHECK(output.positions[0].x == Catch::Approx(5.0F));
    CHECK(output.positions[0].y == Catch::Approx(2.0F));
    CHECK(simnet::length(output.headings[0]) == Catch::Approx(1.0F));
    CHECK(output.headings[0].x == Catch::Approx(output.headings[0].y));
    CHECK(output.hues[0] == 0U);

    REQUIRE(simnet::interpolate_world_snapshots(previous, current, 0.0, output).valid);
    CHECK(output.positions[0].x == 0.0F);
    CHECK(output.hues[0] == 250U);
    REQUIRE(simnet::interpolate_world_snapshots(previous, current, 1.0, output).valid);
    CHECK(output.positions[0].x == 10.0F);
    CHECK(output.hues[0] == 6U);
}

TEST_CASE("snapshot interpolation uses the current entity population", "[snapshot][interpolation]")
{
    auto const previous = snapshot(
        20U,
        {
            {1U, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1U},
            {2U, {2.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 2U},
        }
    );
    auto const current = snapshot(
        21U,
        {
            {2U, {4.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 4U},
            {3U, {6.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 6U},
        }
    );
    auto output = simnet::WorldSnapshot{};

    REQUIRE(simnet::interpolate_world_snapshots(previous, current, 0.5, output).valid);
    CHECK(output.ids == std::vector<simnet::EntityNetId>{2U, 3U});
    CHECK(output.positions[0].x == Catch::Approx(3.0F));
    CHECK(output.positions[1].x == 6.0F);
}

TEST_CASE(
    "snapshot interpolation rejects invalid input without changing output",
    "[snapshot][interpolation]"
)
{
    auto previous = snapshot(
        1U,
        {
            {1U, {}, {1.0F, 0.0F, 0.0F}, 1U},
        }
    );
    auto const current = snapshot(
        2U,
        {
            {1U, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 2U},
        }
    );
    auto output = current;
    auto const before = output.positions.front();

    previous.positions.clear();
    CHECK_FALSE(simnet::interpolate_world_snapshots(previous, current, 0.5, output).valid);
    CHECK(output.positions.front().x == before.x);
    CHECK(output.positions.front().y == before.y);
    CHECK(output.positions.front().z == before.z);
    CHECK_FALSE(
        simnet::interpolate_world_snapshots(
            current,
            current,
            std::numeric_limits<double>::quiet_NaN(),
            output
        )
            .valid
    );
    CHECK(output.positions.front().x == before.x);
    CHECK(output.positions.front().y == before.y);
    CHECK(output.positions.front().z == before.z);
}

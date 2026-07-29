#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

import simnet.app_camera;
import simnet.core;

namespace
{
    [[nodiscard]] bool finite_pose(simnet::app::LockedChaseCameraPose const& pose)
    {
        return simnet::is_finite(pose.position)
            && simnet::is_finite(pose.target)
            && simnet::is_finite(pose.up);
    }
}

TEST_CASE("locked chase camera remains finite at extreme pitch", "[camera][player]")
{
    auto constexpr pitch = 1.483529864F;
    for (auto const sign : std::array { -1.0F, 1.0F }) {
        auto const pose = simnet::app::locked_chase_camera_pose(
            { 2.0F, 3.0F, 4.0F },
            {
                .y = sign * std::sin(pitch),
                .z = std::cos(pitch),
            }
        );
        REQUIRE(finite_pose(pose));
        CHECK(simnet::length(pose.up) == Catch::Approx(1.0F).margin(0.00001F));
        CHECK(simnet::length(pose.target - pose.position) > 0.0F);
        auto const view_direction = simnet::normalize_or(
            pose.target - pose.position,
            simnet::Vec3f { .z = 1.0F }
        );
        CHECK(std::abs(simnet::dot(pose.up, view_direction)) < 0.5F);
    }
}

TEST_CASE("locked chase camera sanitizes degenerate presentation state", "[camera][player]")
{
    auto const invalid = std::numeric_limits<float>::quiet_NaN();
    for (auto const heading : std::array {
        simnet::Vec3f {},
        simnet::Vec3f { .y = 1.0F },
        simnet::Vec3f { .y = -1.0F },
        simnet::Vec3f { .x = invalid, .y = invalid, .z = invalid },
    }) {
        auto const pose = simnet::app::locked_chase_camera_pose(
            { .x = invalid },
            heading
        );
        REQUIRE(finite_pose(pose));
        CHECK(simnet::length(pose.up) == Catch::Approx(1.0F).margin(0.00001F));
        CHECK(simnet::length(pose.target - pose.position) > 0.0F);
    }
}

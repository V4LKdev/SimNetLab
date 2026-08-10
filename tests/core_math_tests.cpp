#include <catch2/catch_test_macros.hpp>

import simnet.core;

TEST_CASE("core cross product preserves right-handed component order", "[core][math]")
{
    auto constexpr x_axis = simnet::Vec3f{.x = 1.0F};
    auto constexpr y_axis = simnet::Vec3f{.y = 1.0F};

    auto constexpr positive_z = simnet::cross(x_axis, y_axis);
    CHECK(positive_z.x == 0.0F);
    CHECK(positive_z.y == 0.0F);
    CHECK(positive_z.z == 1.0F);

    auto constexpr negative_z = simnet::cross(y_axis, x_axis);
    CHECK(negative_z.x == 0.0F);
    CHECK(negative_z.y == 0.0F);
    CHECK(negative_z.z == -1.0F);

    auto constexpr result = simnet::cross(
        simnet::Vec3f{.x = 2.0F, .y = 3.0F, .z = 4.0F},
        simnet::Vec3f{.x = 5.0F, .y = 6.0F, .z = 7.0F}
    );
    CHECK(result.x == -3.0F);
    CHECK(result.y == 6.0F);
    CHECK(result.z == -3.0F);
}

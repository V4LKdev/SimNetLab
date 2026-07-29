#include <catch2/catch_test_macros.hpp>

#include <array>

import simnet.core;
import simnet.render;

TEST_CASE("render entity views require matching SoA lengths", "[render]")
{
    auto const ids = std::array<simnet::EntityNetId, 2> { 1, 2 };
    auto const positions = std::array<simnet::Vec3f, 2> {};
    auto const headings = std::array<simnet::Vec3f, 2> {};
    auto const hues = std::array<std::uint8_t, 2> {};

    auto view = simnet::RenderEntityView {
        .ids = ids,
        .positions = positions,
        .headings = headings,
        .hues = hues,
    };
    CHECK(view.valid());
    CHECK(view.size() == 2);

    view.hues = std::span { hues }.first(1);
    CHECK_FALSE(view.valid());
}

TEST_CASE("viewer contracts keep camera state separate from capabilities", "[render]")
{
    auto const result = simnet::ViewerResult {};
    CHECK(result.camera_mode == simnet::CameraMode::OverviewOrbit);

    auto const capabilities = simnet::ViewerCapabilities {
        .has_networking = true,
        .has_game_camera = true,
    };
    CHECK(capabilities.has_networking);
    CHECK(capabilities.has_game_camera);
    CHECK_FALSE(capabilities.has_stationary_observer);
}

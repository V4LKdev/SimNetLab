#include <catch2/catch_test_macros.hpp>

#include <array>

import simnet.core;
import simnet.spatial;

TEST_CASE("spatial occupied cell keys map back to bounded world cells", "[spatial]")
{
    auto grid = simnet::SpatialGrid {};
    auto scratch = simnet::SpatialGridScratch {};
    auto const bounds = simnet::make_centered_bounds(10.0F);
    simnet::resize_spatial_grid(grid, simnet::make_spatial_grid_settings(bounds, 5.0F));

    auto const positions = std::array<simnet::Vec3f, 2> {
        simnet::Vec3f { .x = -9.0F, .y = -9.0F, .z = -9.0F },
        simnet::Vec3f { .x = 9.0F, .y = 9.0F, .z = 9.0F },
    };
    simnet::build_spatial_grid_serial(grid, scratch, positions);

    REQUIRE(grid.occupied_cells.size() == 2U);
    for (auto const& occupied : grid.occupied_cells) {
        auto const bounds_for_cell = simnet::cell_bounds(
            grid,
            simnet::cell_coord_from_key(grid, occupied.key)
        );
        auto const position = positions[grid.entries[occupied.begin].source_index];
        CHECK(position.x >= bounds_for_cell.min.x);
        CHECK(position.x <= bounds_for_cell.max.x);
        CHECK(position.y >= bounds_for_cell.min.y);
        CHECK(position.y <= bounds_for_cell.max.y);
        CHECK(position.z >= bounds_for_cell.min.z);
        CHECK(position.z <= bounds_for_cell.max.z);
    }
}

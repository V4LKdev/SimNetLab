#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <limits>

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

TEST_CASE("serial spatial builds are deterministic and transactional", "[spatial]")
{
    auto grid = simnet::SpatialGrid {};
    auto scratch = simnet::SpatialGridScratch {};
    simnet::resize_spatial_grid(
        grid,
        simnet::make_spatial_grid_settings(simnet::make_centered_bounds(10.0F), 5.0F)
    );

    auto const positions = std::array<simnet::Vec3f, 3> {
        simnet::Vec3f { .x = -9.0F, .y = -9.0F, .z = -9.0F },
        simnet::Vec3f { .x = -8.0F, .y = -8.0F, .z = -8.0F },
        simnet::Vec3f { .x = 9.0F, .y = 9.0F, .z = 9.0F },
    };
    simnet::build_spatial_grid_serial(grid, scratch, positions);
    REQUIRE(grid.occupied_cells.size() == 2U);
    REQUIRE(grid.entries.size() == positions.size());
    CHECK(grid.entries[0].source_index == 0U);
    CHECK(grid.entries[1].source_index == 1U);
    auto const previous_entries = grid.entries;
    auto const previous_cells = grid.occupied_cells;
    auto const previous_stats = grid.stats;

    auto const matches_previous = [&] {
        if (grid.entries.size() != previous_entries.size()
            || grid.occupied_cells.size() != previous_cells.size()) {
            return false;
        }
        for (std::size_t index = 0; index < grid.entries.size(); ++index) {
            if (grid.entries[index].key != previous_entries[index].key
                || grid.entries[index].source_index != previous_entries[index].source_index) {
                return false;
            }
        }
        for (std::size_t index = 0; index < grid.occupied_cells.size(); ++index) {
            auto const& current = grid.occupied_cells[index];
            auto const& previous = previous_cells[index];
            if (current.key != previous.key || current.begin != previous.begin || current.count != previous.count) {
                return false;
            }
        }
        return true;
    };

    simnet::build_spatial_grid_serial(grid, scratch, positions);
    CHECK(matches_previous());

    auto invalid = positions;
    invalid[1].x = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS(simnet::build_spatial_grid_serial(grid, scratch, invalid));
    CHECK(matches_previous());
    CHECK(grid.stats.entity_count == previous_stats.entity_count);

    simnet::build_spatial_grid_serial(grid, scratch, {});
    CHECK(grid.entries.empty());
    CHECK(grid.occupied_cells.empty());
    CHECK(grid.stats.entity_count == 0U);
}

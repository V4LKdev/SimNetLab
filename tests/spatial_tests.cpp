#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

import simnet.core;
import simnet.spatial;

namespace
{
    using CellContents = std::vector<std::pair<simnet::CellKey, std::vector<simnet::EntityNetId>>>;

    [[nodiscard]] CellContents cell_contents(
        simnet::SpatialGrid const& grid,
        std::span<const simnet::EntityNetId> ids
    )
    {
        auto result = CellContents {};
        for (auto const& cell : grid.occupied_cells) {
            auto cell_ids = std::vector<simnet::EntityNetId> {};
            for (auto offset = std::uint32_t {}; offset < cell.count; ++offset) {
                cell_ids.push_back(ids[grid.entries[cell.begin + offset].source_index]);
            }
            result.emplace_back(cell.key, std::move(cell_ids));
        }
        return result;
    }
}

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

TEST_CASE("bounded spatial build matches comparison ordering", "[spatial]")
{
    auto grid = simnet::SpatialGrid {};
    auto scratch = simnet::SpatialGridScratch {};
    simnet::resize_spatial_grid(
        grid,
        simnet::make_spatial_grid_settings(simnet::make_centered_bounds(10.0F), 5.0F)
    );

    auto const positions = std::array {
        simnet::Vec3f { .x = 9.0F, .y = 9.0F, .z = 9.0F },
        simnet::Vec3f { .x = -9.0F, .y = -9.0F, .z = -9.0F },
        simnet::Vec3f { .x = -8.0F, .y = -8.0F, .z = -8.0F },
        simnet::Vec3f { .x = 1.0F, .y = 1.0F, .z = 1.0F },
    };
    auto const ids = std::array<simnet::EntityNetId, 4> { 40U, 20U, 10U, 30U };

    simnet::build_spatial_grid_serial(grid, scratch, positions, ids);

    REQUIRE(grid.occupied_cells.size() == 3U);
    CHECK(grid.occupied_cells[0].key == 0U);
    CHECK(grid.occupied_cells[0].begin == 0U);
    CHECK(grid.occupied_cells[0].count == 2U);
    CHECK(grid.occupied_cells[1].key == 42U);
    CHECK(grid.occupied_cells[2].key == 63U);
    CHECK(cell_contents(grid, ids) == CellContents {
        { 0U, { 10U, 20U } },
        { 42U, { 30U } },
        { 63U, { 40U } },
    });
    CHECK(scratch.dense_cell_counts.size() == 64U);
}

TEST_CASE("bounded spatial build preserves boundary mapping", "[spatial]")
{
    auto grid = simnet::SpatialGrid {};
    auto scratch = simnet::SpatialGridScratch {};
    simnet::resize_spatial_grid(
        grid,
        simnet::make_spatial_grid_settings(simnet::make_centered_bounds(10.0F), 5.0F)
    );

    auto const positions = std::array {
        simnet::Vec3f { .x = -10.0F, .y = -10.0F, .z = -10.0F },
        simnet::Vec3f { .x = 0.0F, .y = 0.0F, .z = 0.0F },
        simnet::Vec3f { .x = 10.0F, .y = 10.0F, .z = 10.0F },
    };
    simnet::build_spatial_grid_serial(grid, scratch, positions);

    REQUIRE(grid.occupied_cells.size() == 3U);
    CHECK(grid.occupied_cells[0].key == 0U);
    CHECK(grid.occupied_cells[1].key == 42U);
    CHECK(grid.occupied_cells[2].key == 63U);
}

TEST_CASE("bounded spatial output is independent of input order when IDs are supplied", "[spatial]")
{
    auto first_grid = simnet::SpatialGrid {};
    auto second_grid = simnet::SpatialGrid {};
    auto first_scratch = simnet::SpatialGridScratch {};
    auto second_scratch = simnet::SpatialGridScratch {};
    auto const settings =
        simnet::make_spatial_grid_settings(simnet::make_centered_bounds(10.0F), 5.0F);
    simnet::resize_spatial_grid(first_grid, settings);
    simnet::resize_spatial_grid(second_grid, settings);

    auto const first_positions = std::array {
        simnet::Vec3f { .x = -9.0F, .y = -9.0F, .z = -9.0F },
        simnet::Vec3f { .x = 9.0F, .y = 9.0F, .z = 9.0F },
        simnet::Vec3f { .x = -8.0F, .y = -8.0F, .z = -8.0F },
    };
    auto const first_ids = std::array<simnet::EntityNetId, 3> { 30U, 20U, 10U };
    auto const second_positions = std::array {
        first_positions[2],
        first_positions[0],
        first_positions[1],
    };
    auto const second_ids = std::array<simnet::EntityNetId, 3> { 10U, 30U, 20U };

    simnet::build_spatial_grid_serial(first_grid, first_scratch, first_positions, first_ids);
    simnet::build_spatial_grid_serial(second_grid, second_scratch, second_positions, second_ids);

    CHECK(cell_contents(first_grid, first_ids) == cell_contents(second_grid, second_ids));
}

TEST_CASE("oversized spatial grid uses deterministic comparison-sort fallback", "[spatial]")
{
    auto grid = simnet::SpatialGrid {};
    auto scratch = simnet::SpatialGridScratch {};
    simnet::resize_spatial_grid(
        grid,
        simnet::make_spatial_grid_settings(
            { .min = {}, .max = { .x = 65.0F, .y = 65.0F, .z = 65.0F } },
            1.0F
        )
    );

    auto const positions = std::array {
        simnet::Vec3f { .x = 1.1F, .y = 1.1F, .z = 1.1F },
        simnet::Vec3f { .x = 1.2F, .y = 1.2F, .z = 1.2F },
        simnet::Vec3f { .x = 64.9F, .y = 64.9F, .z = 64.9F },
    };
    auto const ids = std::array<simnet::EntityNetId, 3> { 30U, 10U, 20U };

    simnet::build_spatial_grid_serial(grid, scratch, positions, ids);

    CHECK(scratch.dense_cell_counts.empty());
    REQUIRE(grid.occupied_cells.size() == 2U);
    CHECK(cell_contents(grid, ids) == CellContents {
        { 4'291U, { 10U, 30U } },
        { 274'624U, { 20U } },
    });
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

TEST_CASE("ID-aware spatial build rejects mismatched arrays transactionally", "[spatial]")
{
    auto grid = simnet::SpatialGrid {};
    auto scratch = simnet::SpatialGridScratch {};
    simnet::resize_spatial_grid(
        grid,
        simnet::make_spatial_grid_settings(simnet::make_centered_bounds(10.0F), 5.0F)
    );

    auto const positions = std::array {
        simnet::Vec3f { .x = -1.0F, .y = -1.0F, .z = -1.0F },
    };
    auto const ids = std::array<simnet::EntityNetId, 1> { 1U };
    simnet::build_spatial_grid_serial(grid, scratch, positions, ids);
    auto const previous_entries = grid.entries;
    auto const previous_cells = grid.occupied_cells;

    simnet::build_spatial_grid_serial(grid, scratch, {}, {});
    CHECK(grid.entries.empty());
    CHECK(grid.occupied_cells.empty());

    simnet::build_spatial_grid_serial(grid, scratch, positions, ids);
    CHECK_THROWS(simnet::build_spatial_grid_serial(
        grid,
        scratch,
        positions,
        std::span<const simnet::EntityNetId> {}
    ));
    CHECK(grid.entries.size() == previous_entries.size());
    CHECK(grid.occupied_cells.size() == previous_cells.size());
    CHECK(grid.entries[0].source_index == previous_entries[0].source_index);
    CHECK(grid.occupied_cells[0].key == previous_cells[0].key);
}

module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

module simnet.spatial;

import :build;
import :types;
import simnet.core;

namespace
{
    [[nodiscard]] bool valid_bounds(simnet::Aabb3f bounds) noexcept
    {
        return simnet::is_finite(bounds.min)
            && simnet::is_finite(bounds.max)
            && bounds.max.x > bounds.min.x
            && bounds.max.y > bounds.min.y
            && bounds.max.z > bounds.min.z;
    }

    [[nodiscard]] std::uint32_t dimension_for_axis(float min, float max, float cell_size)
    {
        auto const cells = std::ceil((max - min) / cell_size);
        if (cells <= 0.0F || cells > static_cast<float>(std::numeric_limits<std::int32_t>::max())) {
            throw std::runtime_error("invalid spatial grid dimensions");
        }
        return static_cast<std::uint32_t>(cells);
    }

    [[nodiscard]] simnet::CellCoord compute_dimensions(simnet::SpatialGridSettings const& settings)
    {
        return {
            .x = static_cast<std::int32_t>(
                dimension_for_axis(settings.bounds.min.x, settings.bounds.max.x, settings.cell_size)
            ),
            .y = static_cast<std::int32_t>(
                dimension_for_axis(settings.bounds.min.y, settings.bounds.max.y, settings.cell_size)
            ),
            .z = static_cast<std::int32_t>(
                dimension_for_axis(settings.bounds.min.z, settings.bounds.max.z, settings.cell_size)
            ),
        };
    }

    void validate_settings(simnet::SpatialGridSettings const& settings)
    {
        if (!valid_bounds(settings.bounds)) {
            throw std::runtime_error("invalid spatial grid bounds");
        }
        if (!std::isfinite(settings.cell_size) || settings.cell_size <= 0.0F) {
            throw std::runtime_error("invalid spatial grid cell size");
        }
    }

    void validate_grid_ready(simnet::SpatialGrid const& grid)
    {
        validate_settings(grid.settings);
        if (grid.dim_x == 0U || grid.dim_y == 0U || grid.dim_z == 0U) {
            throw std::runtime_error("spatial grid has not been resized");
        }
    }

    void validate_positions(std::span<const simnet::Vec3f> positions)
    {
        if (positions.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("spatial grid position count exceeds supported index range");
        }

        for (auto const position : positions) {
            if (!simnet::is_finite(position)) {
                throw std::runtime_error("spatial grid position contains a non-finite component");
            }
        }
    }

    void validate_flat_key_capacity(std::uint32_t dim_x, std::uint32_t dim_y, std::uint32_t dim_z)
    {
        auto constexpr max = std::numeric_limits<simnet::CellKey>::max();
        if (dim_x != 0U && static_cast<simnet::CellKey>(dim_y) > max / dim_x) {
            throw std::runtime_error("spatial grid dimensions exceed CellKey range");
        }

        auto const xy = static_cast<simnet::CellKey>(dim_x) * dim_y;
        if (xy != 0U && static_cast<simnet::CellKey>(dim_z) > max / xy) {
            throw std::runtime_error("spatial grid dimensions exceed CellKey range");
        }
    }

    [[nodiscard]] std::uint32_t clamp_cell_axis(
        float value,
        float min,
        float cell_size,
        std::uint32_t dimension
    ) noexcept
    {
        auto const relative = (value - min) / cell_size;
        auto const cell = static_cast<std::int64_t>(std::floor(relative));

        if (cell <= 0) {
            return 0;
        }

        auto const max_cell = static_cast<std::int64_t>(dimension - 1U);
        if (cell >= max_cell) {
            return dimension - 1U;
        }

        return static_cast<std::uint32_t>(cell);
    }

    [[nodiscard]] simnet::CellCoord clamp_cell_coord(
        simnet::SpatialGrid const& grid,
        simnet::CellCoord coord
    ) noexcept
    {
        auto const max_x = static_cast<std::int32_t>(grid.dim_x - 1U);
        auto const max_y = static_cast<std::int32_t>(grid.dim_y - 1U);
        auto const max_z = static_cast<std::int32_t>(grid.dim_z - 1U);
        return {
            .x = std::clamp(coord.x, 0, max_x),
            .y = std::clamp(coord.y, 0, max_y),
            .z = std::clamp(coord.z, 0, max_z),
        };
    }

    [[nodiscard]] simnet::CellCoord position_to_cell_coord(
        simnet::SpatialGrid const& grid,
        simnet::Vec3f position
    ) noexcept
    {
        return {
            .x = static_cast<std::int32_t>(
                clamp_cell_axis(position.x, grid.settings.bounds.min.x, grid.settings.cell_size, grid.dim_x)
            ),
            .y = static_cast<std::int32_t>(
                clamp_cell_axis(position.y, grid.settings.bounds.min.y, grid.settings.cell_size, grid.dim_y)
            ),
            .z = static_cast<std::int32_t>(
                clamp_cell_axis(position.z, grid.settings.bounds.min.z, grid.settings.cell_size, grid.dim_z)
            ),
        };
    }

    [[nodiscard]] simnet::CellKey cell_key_from_coord(
        simnet::SpatialGrid const& grid,
        simnet::CellCoord coord
    ) noexcept
    {
        auto const clamped = clamp_cell_coord(grid, coord);
        auto const x = static_cast<simnet::CellKey>(clamped.x);
        auto const y = static_cast<simnet::CellKey>(clamped.y);
        auto const z = static_cast<simnet::CellKey>(clamped.z);
        return x + y * grid.dim_x + z * grid.dim_x * static_cast<simnet::CellKey>(grid.dim_y);
    }

    [[nodiscard]] simnet::CellKey key_for(simnet::SpatialGrid const& grid, simnet::Vec3f position) noexcept
    {
        return cell_key_from_coord(grid, position_to_cell_coord(grid, position));
    }
}

namespace simnet
{
    SpatialGridSettings make_spatial_grid_settings(Aabb3f bounds, float cell_size) noexcept
    {
        return { .bounds = bounds, .cell_size = cell_size };
    }

    void resize_spatial_grid(SpatialGrid& grid, SpatialGridSettings const& settings)
    {
        validate_settings(settings);

        grid.settings = settings;
        auto const dimensions = compute_dimensions(settings);
        grid.dim_x = static_cast<std::uint32_t>(dimensions.x);
        grid.dim_y = static_cast<std::uint32_t>(dimensions.y);
        grid.dim_z = static_cast<std::uint32_t>(dimensions.z);
        validate_flat_key_capacity(grid.dim_x, grid.dim_y, grid.dim_z);
        grid.occupied_cells.clear();
        grid.entries.clear();
        grid.stats = {};
    }

    void prepare_spatial_grid_scratch(
        SpatialGridScratch& scratch,
        std::size_t entity_capacity,
        std::uint32_t worker_count
    )
    {
        static_cast<void>(worker_count);
        scratch.entries.reserve(entity_capacity);
    }

    void build_spatial_grid_serial(
        SpatialGrid& grid,
        SpatialGridScratch& scratch,
        std::span<const Vec3f> positions
    )
    {
        validate_grid_ready(grid);
        validate_positions(positions);

        scratch.entries.clear();
        scratch.entries.reserve(positions.size());

        for (std::size_t index = 0; index < positions.size(); ++index) {
            scratch.entries.push_back({
                .key = key_for(grid, positions[index]),
                .source_index = static_cast<std::uint32_t>(index),
            });
        }

        std::ranges::sort(scratch.entries, [](CellEntry lhs, CellEntry rhs) {
            if (lhs.key == rhs.key) {
                return lhs.source_index < rhs.source_index;
            }
            return lhs.key < rhs.key;
        });

        grid.occupied_cells.clear();
        grid.occupied_cells.reserve(scratch.entries.size());
        grid.entries = scratch.entries;

        auto max_cell_occupancy = std::uint32_t {};
        auto begin = std::uint32_t {};
        while (begin < grid.entries.size()) {
            auto const key = grid.entries[begin].key;
            auto end = begin + 1U;
            while (end < grid.entries.size() && grid.entries[end].key == key) {
                ++end;
            }

            auto const count = end - begin;
            grid.occupied_cells.push_back({
                .key = key,
                .begin = begin,
                .count = count,
            });
            max_cell_occupancy = std::max(max_cell_occupancy, count);
            begin = end;
        }

        auto const occupied_cell_count = static_cast<std::uint32_t>(grid.occupied_cells.size());
        auto const entity_count = static_cast<std::uint32_t>(positions.size());
        auto const average_load = occupied_cell_count == 0U
            ? 0.0F
            : static_cast<float>(entity_count) / static_cast<float>(occupied_cell_count);

        grid.stats = {
            .entity_count = entity_count,
            .occupied_cell_count = occupied_cell_count,
            .max_cell_occupancy = max_cell_occupancy,
            .average_occupied_cell_load = average_load,
        };
    }

    void build_spatial_grid_parallel(
        SpatialGrid& grid,
        SpatialGridScratch& scratch,
        std::span<const Vec3f> positions,
        std::uint32_t worker_count
    )
    {
        static_cast<void>(worker_count);
        build_spatial_grid_serial(grid, scratch, positions);
    }
}

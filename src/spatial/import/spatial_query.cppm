module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

/// @brief Spatial grid query API.
export module simnet.spatial:query;

import :types;
import :build;
import simnet.core;

namespace simnet::spatial_detail
{
    [[nodiscard]] inline bool valid_grid(SpatialGrid const& grid) noexcept
    {
        return grid.dim_x > 0U && grid.dim_y > 0U && grid.dim_z > 0U &&
               grid.settings.cell_size > 0.0F && is_finite(grid.settings.bounds.min) &&
               is_finite(grid.settings.bounds.max) &&
               grid.settings.bounds.max.x > grid.settings.bounds.min.x &&
               grid.settings.bounds.max.y > grid.settings.bounds.min.y &&
               grid.settings.bounds.max.z > grid.settings.bounds.min.z;
    }

    [[nodiscard]] inline CellKey bounded_cell_key(SpatialGrid const& grid, CellCoord coord) noexcept
    {
        auto const x = static_cast<CellKey>(coord.x);
        auto const y = static_cast<CellKey>(coord.y);
        auto const z = static_cast<CellKey>(coord.z);
        return x + y * grid.dim_x + z * grid.dim_x * static_cast<CellKey>(grid.dim_y);
    }

    template <class CellCallback>
    std::uint32_t
    for_each_cell_coordinate(CellCoord minimum, CellCoord maximum, CellCallback&& callback)
    {
        auto const min_x = static_cast<std::uint32_t>(minimum.x);
        auto const min_y = static_cast<std::uint32_t>(minimum.y);
        auto const min_z = static_cast<std::uint32_t>(minimum.z);
        auto const max_x = static_cast<std::uint32_t>(maximum.x);
        auto const max_y = static_cast<std::uint32_t>(maximum.y);
        auto const max_z = static_cast<std::uint32_t>(maximum.z);

        auto count = std::uint32_t{};
        for (auto cell_z = min_z; cell_z <= max_z; ++cell_z)
        {
            for (auto cell_y = min_y; cell_y <= max_y; ++cell_y)
            {
                for (auto cell_x = min_x; cell_x <= max_x; ++cell_x)
                {
                    callback(
                        CellCoord{
                            .x = static_cast<std::int32_t>(cell_x),
                            .y = static_cast<std::int32_t>(cell_y),
                            .z = static_cast<std::int32_t>(cell_z),
                        }
                    );
                    ++count;
                }
            }
        }
        return count;
    }

    [[nodiscard]] inline CellRange const*
    find_cell_range(SpatialGrid const& grid, CellKey key) noexcept
    {
        auto const found = std::ranges::lower_bound(grid.occupied_cells, key, {}, &CellRange::key);
        if (found == grid.occupied_cells.end() || found->key != key)
        {
            return nullptr;
        }
        return &*found;
    }

    [[nodiscard]] inline bool intersects(Aabb3f lhs, Aabb3f rhs) noexcept
    {
        return lhs.min.x <= rhs.max.x && lhs.max.x >= rhs.min.x && lhs.min.y <= rhs.max.y &&
               lhs.max.y >= rhs.min.y && lhs.min.z <= rhs.max.z && lhs.max.z >= rhs.min.z;
    }

    inline void validate_query_grid(SpatialGrid const& grid)
    {
        if (!valid_grid(grid))
        {
            throw std::runtime_error("invalid spatial grid query state");
        }
    }

    inline void validate_radius_query(Vec3f center, float radius)
    {
        if (!is_finite(center))
        {
            throw std::runtime_error("spatial radius query center contains a non-finite component");
        }
        if (!std::isfinite(radius) || radius < 0.0F)
        {
            throw std::runtime_error("spatial radius query radius must be finite and non-negative");
        }
    }

    inline void validate_aabb_query(Aabb3f bounds)
    {
        if (!is_finite(bounds.min) || !is_finite(bounds.max))
        {
            throw std::runtime_error("spatial AABB query contains a non-finite component");
        }
        if (bounds.max.x < bounds.min.x || bounds.max.y < bounds.min.y ||
            bounds.max.z < bounds.min.z)
        {
            throw std::runtime_error("spatial AABB query bounds are inverted");
        }
    }

    template <class CandidateCallback>
    void query_radius_cell(
        SpatialGrid const& grid,
        std::span<const Vec3f> positions,
        Vec3f center,
        float radius_squared,
        CellCoord coord,
        CandidateCallback&& callback
    )
    {
        auto const key = bounded_cell_key(grid, coord);
        auto const* range = find_cell_range(grid, key);
        if (range == nullptr)
        {
            return;
        }

        auto const end = range->begin + range->count;
        for (auto entry_index = range->begin; entry_index < end; ++entry_index)
        {
            auto const source_index = grid.entries[entry_index].source_index;
            if (source_index >= positions.size())
            {
                continue;
            }

            auto const offset = positions[source_index] - center;
            if (length_squared(offset) <= radius_squared)
            {
                callback(source_index);
            }
        }
    }

    template <class CandidateCallback>
    void query_aabb_cell(
        SpatialGrid const& grid,
        std::span<const Vec3f> positions,
        Aabb3f bounds,
        CellCoord coord,
        CandidateCallback&& callback
    )
    {
        auto const key = bounded_cell_key(grid, coord);
        auto const* range = find_cell_range(grid, key);
        if (range == nullptr)
        {
            return;
        }

        auto const end = range->begin + range->count;
        for (auto entry_index = range->begin; entry_index < end; ++entry_index)
        {
            auto const source_index = grid.entries[entry_index].source_index;
            if (source_index < positions.size() && contains(bounds, positions[source_index]))
            {
                callback(source_index);
            }
        }
    }
}

export namespace simnet
{
    /// Calls callback for every bounded grid cell inspected by a radius query.
    template <class Callback>
    std::uint32_t
    for_each_radius_cell(SpatialGrid const& grid, Vec3f center, float radius, Callback&& callback)
    {
        spatial_detail::validate_query_grid(grid);
        spatial_detail::validate_radius_query(center, radius);

        auto const query_bounds = Aabb3f{
            .min = center - Vec3f{radius, radius, radius},
            .max = center + Vec3f{radius, radius, radius},
        };

        if (!spatial_detail::intersects(grid.settings.bounds, query_bounds))
        {
            return 0U;
        }

        return spatial_detail::for_each_cell_coordinate(
            cell_coord_for_position(grid, query_bounds.min),
            cell_coord_for_position(grid, query_bounds.max),
            callback
        );
    }

    /// Calls callback with source indices whose positions are inside the radius.
    /// The positions span must match the finite source positions used to build the grid.
    /// Returns the number of bounded grid cells inspected.
    template <class Callback>
    std::uint32_t query_radius(
        SpatialGrid const& grid,
        std::span<const Vec3f> positions,
        Vec3f center,
        float radius,
        Callback&& callback
    )
    {
        auto const radius_squared = radius * radius;
        return for_each_radius_cell(
            grid,
            center,
            radius,
            [&](CellCoord coord)
            {
                spatial_detail::query_radius_cell(
                    grid,
                    positions,
                    center,
                    radius_squared,
                    coord,
                    callback
                );
            }
        );
    }

    /// Calls callback with source indices whose positions are inside the bounds.
    /// The positions span must match the finite source positions used to build the grid.
    template <class Callback>
    void query_aabb(
        SpatialGrid const& grid,
        std::span<const Vec3f> positions,
        Aabb3f bounds,
        Callback&& callback
    )
    {
        spatial_detail::validate_query_grid(grid);
        spatial_detail::validate_aabb_query(bounds);

        if (!spatial_detail::intersects(grid.settings.bounds, bounds))
        {
            return;
        }

        static_cast<void>(spatial_detail::for_each_cell_coordinate(
            cell_coord_for_position(grid, bounds.min),
            cell_coord_for_position(grid, bounds.max),
            [&](CellCoord coord)
            {
                spatial_detail::query_aabb_cell(grid, positions, bounds, coord, callback);
            }
        ));
    }
}

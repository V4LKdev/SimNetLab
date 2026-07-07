module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

/// @brief Spatial grid query API.
export module simnet.spatial:query;

import :types;
import simnet.core;

namespace simnet::spatial_detail
{
    [[nodiscard]] inline bool valid_grid(SpatialGrid const& grid) noexcept
    {
        return grid.dim_x > 0U
            && grid.dim_y > 0U
            && grid.dim_z > 0U
            && grid.settings.cell_size > 0.0F
            && is_finite(grid.settings.bounds.min)
            && is_finite(grid.settings.bounds.max)
            && grid.settings.bounds.max.x > grid.settings.bounds.min.x
            && grid.settings.bounds.max.y > grid.settings.bounds.min.y
            && grid.settings.bounds.max.z > grid.settings.bounds.min.z;
    }

    [[nodiscard]] inline std::uint32_t clamp_axis(
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

    [[nodiscard]] inline CellCoord clamp_cell_coord(
        SpatialGrid const& grid,
        CellCoord coord
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

    [[nodiscard]] inline CellKey cell_key_from_coord(
        SpatialGrid const& grid,
        CellCoord coord
    ) noexcept
    {
        auto const clamped = clamp_cell_coord(grid, coord);
        auto const x = static_cast<CellKey>(clamped.x);
        auto const y = static_cast<CellKey>(clamped.y);
        auto const z = static_cast<CellKey>(clamped.z);
        return x + y * grid.dim_x + z * grid.dim_x * static_cast<CellKey>(grid.dim_y);
    }

    [[nodiscard]] inline CellCoord position_to_cell_coord(
        SpatialGrid const& grid,
        Vec3f position
    ) noexcept
    {
        return {
            .x = static_cast<std::int32_t>(
                clamp_axis(position.x, grid.settings.bounds.min.x, grid.settings.cell_size, grid.dim_x)
            ),
            .y = static_cast<std::int32_t>(
                clamp_axis(position.y, grid.settings.bounds.min.y, grid.settings.cell_size, grid.dim_y)
            ),
            .z = static_cast<std::int32_t>(
                clamp_axis(position.z, grid.settings.bounds.min.z, grid.settings.cell_size, grid.dim_z)
            ),
        };
    }

    [[nodiscard]] inline CellRange const* find_cell_range(SpatialGrid const& grid, CellKey key) noexcept
    {
        auto const found = std::ranges::lower_bound(
            grid.occupied_cells,
            key,
            {},
            &CellRange::key
        );
        if (found == grid.occupied_cells.end() || found->key != key) {
            return nullptr;
        }
        return &*found;
    }

    [[nodiscard]] inline bool intersects(Aabb3f lhs, Aabb3f rhs) noexcept
    {
        return lhs.min.x <= rhs.max.x && lhs.max.x >= rhs.min.x
            && lhs.min.y <= rhs.max.y && lhs.max.y >= rhs.min.y
            && lhs.min.z <= rhs.max.z && lhs.max.z >= rhs.min.z;
    }

    inline void validate_query_grid(SpatialGrid const& grid)
    {
        if (!valid_grid(grid)) {
            throw std::runtime_error("invalid spatial grid query state");
        }
    }

    inline void validate_radius_query(Vec3f center, float radius)
    {
        if (!is_finite(center)) {
            throw std::runtime_error("spatial radius query center contains a non-finite component");
        }
        if (!std::isfinite(radius) || radius < 0.0F) {
            throw std::runtime_error("spatial radius query radius must be finite and non-negative");
        }
    }

    inline void validate_aabb_query(Aabb3f bounds)
    {
        if (!is_finite(bounds.min) || !is_finite(bounds.max)) {
            throw std::runtime_error("spatial AABB query contains a non-finite component");
        }
        if (bounds.max.x < bounds.min.x || bounds.max.y < bounds.min.y || bounds.max.z < bounds.min.z) {
            throw std::runtime_error("spatial AABB query bounds are inverted");
        }
    }
}

export namespace simnet
{
    /// Calls callback with source indices whose positions are inside the radius.
    template <class Callback>
    void query_radius(
        SpatialGrid const& grid,
        std::span<const Vec3f> positions,
        Vec3f center,
        float radius,
        Callback&& callback
    )
    {
        spatial_detail::validate_query_grid(grid);
        spatial_detail::validate_radius_query(center, radius);

        auto const query_bounds = Aabb3f {
            .min = center - Vec3f { radius, radius, radius },
            .max = center + Vec3f { radius, radius, radius },
        };

        if (!spatial_detail::intersects(grid.settings.bounds, query_bounds)) {
            return;
        }

        auto const min_x = spatial_detail::clamp_axis(
            query_bounds.min.x,
            grid.settings.bounds.min.x,
            grid.settings.cell_size,
            grid.dim_x
        );
        auto const min_y = spatial_detail::clamp_axis(
            query_bounds.min.y,
            grid.settings.bounds.min.y,
            grid.settings.cell_size,
            grid.dim_y
        );
        auto const min_z = spatial_detail::clamp_axis(
            query_bounds.min.z,
            grid.settings.bounds.min.z,
            grid.settings.cell_size,
            grid.dim_z
        );
        auto const max_x = spatial_detail::clamp_axis(
            query_bounds.max.x,
            grid.settings.bounds.min.x,
            grid.settings.cell_size,
            grid.dim_x
        );
        auto const max_y = spatial_detail::clamp_axis(
            query_bounds.max.y,
            grid.settings.bounds.min.y,
            grid.settings.cell_size,
            grid.dim_y
        );
        auto const max_z = spatial_detail::clamp_axis(
            query_bounds.max.z,
            grid.settings.bounds.min.z,
            grid.settings.cell_size,
            grid.dim_z
        );

        auto const radius_squared = radius * radius;
        for (auto z = min_z;; ++z) {
            for (auto y = min_y;; ++y) {
                for (auto x = min_x;; ++x) {
                    auto const key = spatial_detail::cell_key_from_coord(grid, {
                        .x = static_cast<std::int32_t>(x),
                        .y = static_cast<std::int32_t>(y),
                        .z = static_cast<std::int32_t>(z),
                    });
                    auto const* range = spatial_detail::find_cell_range(grid, key);
                    if (range == nullptr) {
                        if (x == max_x) {
                            break;
                        }
                        continue;
                    }

                    auto const end = range->begin + range->count;
                    for (auto entry_index = range->begin; entry_index < end; ++entry_index) {
                        auto const source_index = grid.entries[entry_index].source_index;
                        if (source_index >= positions.size()) {
                            continue;
                        }

                        auto const offset = positions[source_index] - center;
                        if (length_squared(offset) <= radius_squared) {
                            callback(source_index);
                        }
                    }

                    if (x == max_x) {
                        break;
                    }
                }
                if (y == max_y) {
                    break;
                }
            }
            if (z == max_z) {
                break;
            }
        }
    }

    /// Calls callback with source indices whose positions are inside the bounds.
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

        if (!spatial_detail::intersects(grid.settings.bounds, bounds)) {
            return;
        }

        auto const min_x = spatial_detail::clamp_axis(
            bounds.min.x,
            grid.settings.bounds.min.x,
            grid.settings.cell_size,
            grid.dim_x
        );
        auto const min_y = spatial_detail::clamp_axis(
            bounds.min.y,
            grid.settings.bounds.min.y,
            grid.settings.cell_size,
            grid.dim_y
        );
        auto const min_z = spatial_detail::clamp_axis(
            bounds.min.z,
            grid.settings.bounds.min.z,
            grid.settings.cell_size,
            grid.dim_z
        );
        auto const max_x = spatial_detail::clamp_axis(
            bounds.max.x,
            grid.settings.bounds.min.x,
            grid.settings.cell_size,
            grid.dim_x
        );
        auto const max_y = spatial_detail::clamp_axis(
            bounds.max.y,
            grid.settings.bounds.min.y,
            grid.settings.cell_size,
            grid.dim_y
        );
        auto const max_z = spatial_detail::clamp_axis(
            bounds.max.z,
            grid.settings.bounds.min.z,
            grid.settings.cell_size,
            grid.dim_z
        );

        for (auto z = min_z;; ++z) {
            for (auto y = min_y;; ++y) {
                for (auto x = min_x;; ++x) {
                    auto const key = spatial_detail::cell_key_from_coord(grid, {
                        .x = static_cast<std::int32_t>(x),
                        .y = static_cast<std::int32_t>(y),
                        .z = static_cast<std::int32_t>(z),
                    });
                    auto const* range = spatial_detail::find_cell_range(grid, key);
                    if (range == nullptr) {
                        if (x == max_x) {
                            break;
                        }
                        continue;
                    }

                    auto const end = range->begin + range->count;
                    for (auto entry_index = range->begin; entry_index < end; ++entry_index) {
                        auto const source_index = grid.entries[entry_index].source_index;
                        if (source_index < positions.size() && contains(bounds, positions[source_index])) {
                            callback(source_index);
                        }
                    }

                    if (x == max_x) {
                        break;
                    }
                }
                if (y == max_y) {
                    break;
                }
            }
            if (z == max_z) {
                break;
            }
        }
    }
}

module;

#include <cstddef>
#include <cstdint>
#include <span>

/// @brief Spatial grid build API.
export module simnet.spatial:build;

import :types;
import simnet.core;

export namespace simnet
{
    /// Returns spatial grid settings for bounded world-space positions.
    [[nodiscard]] SpatialGridSettings make_spatial_grid_settings(Aabb3f bounds, float cell_size) noexcept;

    /// Applies settings, computes dimensions, and clears grid contents.
    void resize_spatial_grid(SpatialGrid& grid, SpatialGridSettings const& settings);

    /// Reserves reusable build storage.
    void prepare_spatial_grid_scratch(
        SpatialGridScratch& scratch,
        std::size_t entity_capacity,
        std::uint32_t worker_count
    );

    /// Rebuilds the sparse sorted grid serially.
    void build_spatial_grid_serial(
        SpatialGrid& grid,
        SpatialGridScratch& scratch,
        std::span<const Vec3f> positions
    );

    /// Rebuilds the sparse sorted grid with a parallel-ready API.
    void build_spatial_grid_parallel(
        SpatialGrid& grid,
        SpatialGridScratch& scratch,
        std::span<const Vec3f> positions,
        std::uint32_t worker_count
    );
}

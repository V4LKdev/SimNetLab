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

    /// Begins a phased rebuild without storing the positions span.
    void begin_spatial_grid_build(
        SpatialGrid& grid,
        SpatialGridScratch& scratch,
        std::span<const Vec3f> positions
    );

    /// Builds cell entries for one externally scheduled worker shard.
    void build_spatial_grid_worker_entries(
        SpatialGrid const& grid,
        SpatialGridWorkerScratch& worker_scratch,
        std::span<const Vec3f> positions,
        std::uint32_t begin_index,
        std::uint32_t end_index
    );

    /// Merges worker entries, sorts them, compacts ranges, and updates stats.
    void finish_spatial_grid_build(
        SpatialGrid& grid,
        SpatialGridScratch& scratch
    );

    /// Rebuilds the sparse sorted grid with one worker.
    void build_spatial_grid_serial(
        SpatialGrid& grid,
        SpatialGridScratch& scratch,
        std::span<const Vec3f> positions
    );
}

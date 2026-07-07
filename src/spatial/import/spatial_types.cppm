module;

#include <cstdint>
#include <vector>

/// @brief Spatial grid data contracts.
export module simnet.spatial:types;

import simnet.core;

export namespace simnet
{
    /// Flattened bounded cell key.
    using CellKey = std::uint64_t;

    /// Integer grid cell coordinate.
    struct CellCoord
    {
        std::int32_t x {};
        std::int32_t y {};
        std::int32_t z {};
    };

    /// Source index assigned to a spatial cell.
    struct CellEntry
    {
        CellKey key {};
        std::uint32_t source_index {};
    };

    /// Contiguous range of entries for one occupied cell.
    struct CellRange
    {
        CellKey key {};
        std::uint32_t begin {};
        std::uint32_t count {};
    };

    /// Bounded uniform-grid settings.
    struct SpatialGridSettings
    {
        Aabb3f bounds {};
        float cell_size { 20.0F };
    };

    /// Occupancy statistics for the last grid build.
    struct SpatialGridStats
    {
        std::uint32_t entity_count {};
        std::uint32_t occupied_cell_count {};
        std::uint32_t max_cell_occupancy {};
        float average_occupied_cell_load {};
    };

    /// Sparse sorted uniform grid view for one tick.
    struct SpatialGrid
    {
        SpatialGridSettings settings {};
        std::uint32_t dim_x {};
        std::uint32_t dim_y {};
        std::uint32_t dim_z {};
        std::vector<CellRange> occupied_cells;
        std::vector<CellEntry> entries;
        SpatialGridStats stats {};
    };

    /// Per-worker build storage.
    struct SpatialGridWorkerScratch
    {
        std::vector<CellEntry> entries;
    };

    /// Reusable build storage.
    struct SpatialGridScratch
    {
        std::vector<SpatialGridWorkerScratch> workers;
        std::vector<CellEntry> entries;
    };
}

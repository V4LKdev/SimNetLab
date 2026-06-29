/// \file   visualization.cppm
/// \brief  Generic raylib simulation viewer, data‑driven and decoupled from simulation internals.
///
/// \details
/// Provides draw‑instance structs, frame/statistics inputs, and a move‑only
/// RaylibSimulationViewer that renders boids, debug overlays, and a basic UI.

module;
#include <cstdint>
#include <cstddef>
#include <span>
#include <memory>
#include <string>

export module simnet.visualization;

import simnet.utils;
import simnet.game.shared;

export namespace simnet::visualization {
    /// Window creation parameters.
    struct VisualizerConfig {
        int width = 1920;
        int height = 1080;
        std::string title = "SimNetLab";
        std::string role_label = "Viewer";
        int target_fps = 0;
    };

    /// Complete draw frame - the only required data for the viewer.
    struct SimulationViewFrame {
        std::span<const game::shared::BoidViewInstance> boids{};
        float world_half = 1.0f;
        std::uint64_t tick = 0;
        float interpolation_alpha = 0.0f;
    };

    /// Optional text‑overlay statistics.
    struct SimulationViewStats {
        /// Total number of boids in the simulation.
        std::uint32_t active_boids = 0;
        /// Total number of boids that should be in the simulation (for example, if some are culled or not yet spawned).
        std::uint32_t target_boids = 0;
        float avg_speed = 0.0f;
        float max_speed = 0.0f;
        float avg_steer = 0.0f;
        float polarization = 0.0f;
    };

    /// Optional spatial‑grid debug overlay data.
    struct SpatialGridDebug {
        /// Grid occupancy data, in row‑major order (x fastest, then y, then z).
        std::span<const std::uint32_t> cell_counts;
        /// Grid dimensions (x, y, z).
        std::uint32_t dim_x = 0;
        std::uint32_t dim_y = 0;
        std::uint32_t dim_z = 0;
        float cell_size = 1.0f;
        float world_half = 1.0f;
        /// Maximum number of cells to draw (for performance reasons).
        std::uint32_t max_cells_to_draw = 10000;
        /// Whether to draw empty cells (cell_counts == 0) or not (could be made a ui toggle possibly).
        bool draw_empty_cells = false;
    };

    /// Optional global/effective rule values for tracked-boid gizmos.
    struct SimulationRuleDebug {
        float body_radius = 0.15f;
        float separation_radius = 0.0f;
        float local_alignment_radius = 0.0f;
        float cohesion_radius = 0.0f;
        float fov_cos = -1.0f;
        float exact_query_radius = 0.0f;
    };

    /// Optional debug inputs for one rendered frame.
    struct SimulationDebugFrame {
        const SpatialGridDebug *spatial_grid = nullptr;
        const SimulationRuleDebug *rules = nullptr;
    };

    /// Move‑only raylib viewer that consumes generic draw data.
    class RaylibSimulationViewer {
    public:
        /// Creates a window, loads assets, prepares renderer.
        explicit RaylibSimulationViewer(VisualizerConfig config = {});

        /// Unloads assets and closes the window.
        ~RaylibSimulationViewer();

        // Non‑copyable.
        RaylibSimulationViewer(const RaylibSimulationViewer &) = delete;

        RaylibSimulationViewer &operator=(const RaylibSimulationViewer &) = delete;

        // Move‑only.
        RaylibSimulationViewer(RaylibSimulationViewer &&) noexcept;

        RaylibSimulationViewer &operator=(RaylibSimulationViewer &&) noexcept;

        /// Returns true when the window’s close button was pressed.
        [[nodiscard]] bool should_close() const noexcept;

        /// Returns true when draw() should receive SpatialGridDebug this frame.
        [[nodiscard]] bool wants_spatial_debug() const noexcept;

        /// Renders one frame. stats and debug may be nullptr.
        void draw(const SimulationViewFrame &frame,
                  const SimulationViewStats *stats = nullptr,
                  const SimulationDebugFrame *debug = nullptr);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}

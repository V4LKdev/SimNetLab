/// \file   raylib_debug_gizmos.cppm
/// \brief  Private raylib debug drawing helpers for tracked boids and grids.

module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include <raylib.h>
#include <raymath.h>

export module simnet.visualization:raylib_debug_gizmos;

import simnet.game.shared;
import simnet.utils;
import simnet.visualization;

export namespace simnet::visualization::detail {
    /// Internal tracked-boid debug draw toggles.
    struct TrackedGizmoState {
        bool trail = true;
        bool body = true;
        bool separation = true;
        bool alignment = true;
        bool cohesion = true;
        bool fov = true;
        bool query_cells = true;
    };

    /// Converts the shared math vector to raylib's vector type.
    [[nodiscard]] Vector3 to_raylib(const utils::Vec3 &value) noexcept
    {
        return Vector3{value.x(), value.y(), value.z()};
    }

    /// Returns a unit vector, or the default boid forward direction.
    [[nodiscard]] Vector3 normalized_or_forward(Vector3 value) noexcept
    {
        const float length_sq = Vector3LengthSqr(value);
        if (!std::isfinite(length_sq) || length_sq <= 0.000001f) {
            return Vector3{1.0f, 0.0f, 0.0f};
        }
        return Vector3Scale(value, 1.0f / std::sqrt(length_sq));
    }

    /// Returns value when positive and finite, otherwise fallback.
    [[nodiscard]] float positive_or(float value, float fallback) noexcept
    {
        return value > 0.0f && std::isfinite(value) ? value : fallback;
    }

    /// Draws the selected boid marker.
    void draw_tracked_boid_marker(const game::shared::BoidViewInstance &boid, float boid_radius)
    {
        DrawSphereWires(to_raylib(boid.position), boid_radius * 5.0f, 16, 10, Color{255, 235, 120, 255});
    }

    /// Draws the tracked boid's recent world-space path.
    void draw_tracked_path(std::span<const Vector3> trail)
    {
        if (trail.size() < 2) {
            return;
        }

        for (std::size_t index = 1; index < trail.size(); ++index) {
            const float alpha = static_cast<float>(index) / static_cast<float>(trail.size() - 1);
            const Vector3 start = trail[index - 1];
            const Vector3 end = trail[index];
            if (Vector3DistanceSqr(start, end) <= 0.000001f) {
                continue;
            }
            const Color color{
                150,
                18,
                24,
                static_cast<unsigned char>(70.0f + alpha * 170.0f)
            };
            DrawCylinderEx(start, end, 0.12f, 0.12f, 8, color);
        }
    }

    /// Draws rule radii and field-of-view helpers for the selected boid.
    void draw_tracked_rule_gizmos(const game::shared::BoidViewInstance &boid,
                                  const SimulationRuleDebug &rules,
                                  float fallback_body_radius,
                                  const TrackedGizmoState &gizmos)
    {
        const Vector3 position = to_raylib(boid.position);
        const Vector3 heading = normalized_or_forward(to_raylib(boid.heading));

        if (gizmos.body) {
            const float body = positive_or(rules.body_radius, fallback_body_radius);
            DrawSphereWires(position, body, 12, 8, Color{255, 235, 120, 180});
        }

        if (gizmos.separation && rules.separation_radius > 0.0f) {
            DrawSphereWires(position, rules.separation_radius, 24, 12, Color{230, 94, 94, 96});
        }
        if (gizmos.alignment && rules.local_alignment_radius > 0.0f) {
            DrawSphereWires(position, rules.local_alignment_radius, 24, 12, Color{92, 174, 235, 84});
        }
        if (gizmos.cohesion && rules.cohesion_radius > 0.0f) {
            DrawSphereWires(position, rules.cohesion_radius, 24, 12, Color{124, 214, 156, 70});
        }
        if (!gizmos.fov) {
            return;
        }

        const float radius = positive_or(
            rules.local_alignment_radius,
            positive_or(rules.exact_query_radius, positive_or(rules.separation_radius, 8.0f)));
        const float cos_half_angle = std::clamp(rules.fov_cos, -0.98f, 0.98f);
        const float sin_half_angle = std::sqrt(std::max(0.0f, 1.0f - cos_half_angle * cos_half_angle));

        Vector3 right = Vector3CrossProduct(heading, Vector3{0.0f, 1.0f, 0.0f});
        if (Vector3LengthSqr(right) <= 0.000001f) {
            right = Vector3CrossProduct(heading, Vector3{1.0f, 0.0f, 0.0f});
        }
        right = normalized_or_forward(right);
        const Vector3 up = normalized_or_forward(Vector3CrossProduct(right, heading));

        constexpr int segments = 8;
        Vector3 previous{};
        for (int index = 0; index <= segments; ++index) {
            const float angle = 2.0f * PI * static_cast<float>(index) / static_cast<float>(segments);
            const Vector3 lateral = Vector3Add(Vector3Scale(right, std::cos(angle)), Vector3Scale(up, std::sin(angle)));
            const Vector3 direction = normalized_or_forward(
                Vector3Add(Vector3Scale(heading, cos_half_angle), Vector3Scale(lateral, sin_half_angle)));
            const Vector3 point = Vector3Add(position, Vector3Scale(direction, radius));
            DrawLine3D(position, point, Color{255, 205, 120, 86});
            if (index > 0) {
                DrawLine3D(previous, point, Color{255, 205, 120, 70});
            }
            previous = point;
        }
    }

    /// Draws likely query cells around the selected boid.
    void draw_tracked_query_cells(const game::shared::BoidViewInstance &boid,
                                  const SpatialGridDebug &debug,
                                  const SimulationRuleDebug &rules)
    {
        if (debug.dim_x == 0 || debug.dim_y == 0 || debug.dim_z == 0 || debug.cell_size <= 0.0f) {
            return;
        }

        const float radius = std::max({
            rules.exact_query_radius,
            rules.separation_radius,
            rules.local_alignment_radius
        });
        if (radius <= 0.0f || !std::isfinite(radius)) {
            return;
        }

        const auto axis_cell = [](float value, float world_half, float cell_size, std::uint32_t dim) {
            const auto raw = static_cast<int>(std::floor((value + world_half) / cell_size));
            return std::clamp(raw, 0, static_cast<int>(dim) - 1);
        };

        const int cx = axis_cell(boid.position.x(), debug.world_half, debug.cell_size, debug.dim_x);
        const int cy = axis_cell(boid.position.y(), debug.world_half, debug.cell_size, debug.dim_y);
        const int cz = axis_cell(boid.position.z(), debug.world_half, debug.cell_size, debug.dim_z);
        const int delta = static_cast<int>(std::ceil(radius / debug.cell_size));

        const int min_x = std::max(0, cx - delta);
        const int max_x = std::min(static_cast<int>(debug.dim_x) - 1, cx + delta);
        const int min_y = std::max(0, cy - delta);
        const int max_y = std::min(static_cast<int>(debug.dim_y) - 1, cy + delta);
        const int min_z = std::max(0, cz - delta);
        const int max_z = std::min(static_cast<int>(debug.dim_z) - 1, cz + delta);

        for (int z = min_z; z <= max_z; ++z) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int x = min_x; x <= max_x; ++x) {
                    const Vector3 center{
                        -debug.world_half + (static_cast<float>(x) + 0.5f) * debug.cell_size,
                        -debug.world_half + (static_cast<float>(y) + 0.5f) * debug.cell_size,
                        -debug.world_half + (static_cast<float>(z) + 0.5f) * debug.cell_size
                    };
                    DrawCubeWires(center, debug.cell_size, debug.cell_size, debug.cell_size, Color{180, 205, 235, 72});
                }
            }
        }
    }

    /// Draws occupied spatial cells, with capped optional empty-cell drawing.
    void draw_spatial_cells(const SpatialGridDebug &debug)
    {
        const std::uint64_t total_cells =
                static_cast<std::uint64_t>(debug.dim_x) * debug.dim_y * debug.dim_z;
        if (total_cells == 0 || debug.cell_counts.size() < total_cells) {
            return;
        }

        const bool draw_empty = debug.draw_empty_cells && total_cells <= debug.max_cells_to_draw;
        std::uint32_t drawn = 0;

        for (std::uint32_t z = 0; z < debug.dim_z; ++z) {
            for (std::uint32_t y = 0; y < debug.dim_y; ++y) {
                for (std::uint32_t x = 0; x < debug.dim_x; ++x) {
                    const std::uint64_t index =
                            x + static_cast<std::uint64_t>(y) * debug.dim_x +
                            static_cast<std::uint64_t>(z) * debug.dim_x * debug.dim_y;
                    const std::uint32_t count = debug.cell_counts[static_cast<std::size_t>(index)];
                    if (count == 0 && !draw_empty) {
                        continue;
                    }
                    if (drawn >= debug.max_cells_to_draw) {
                        return;
                    }

                    const Vector3 center{
                        -debug.world_half + (static_cast<float>(x) + 0.5f) * debug.cell_size,
                        -debug.world_half + (static_cast<float>(y) + 0.5f) * debug.cell_size,
                        -debug.world_half + (static_cast<float>(z) + 0.5f) * debug.cell_size
                    };

                    // Color intensity is based on the number of boids in the cell, capped at 220 alpha.
                    const Color color = count == 0
                                            ? Color{76, 86, 102, 34}
                                            : Color{
                                                70, 165, 235,
                                                static_cast<unsigned char>(std::min(220u, 60u + count * 10u))
                                            };
                    DrawCubeWires(center, debug.cell_size, debug.cell_size, debug.cell_size, color);
                    ++drawn;
                }
            }
        }
    }
}

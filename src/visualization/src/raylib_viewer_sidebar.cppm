/// \file   raylib_viewer_sidebar.cppm
/// \brief  Private manual sidebar drawing helpers for the raylib viewer.

module;

#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>

#include <raylib.h>

export module simnet.visualization:raylib_viewer_sidebar;

import :raylib_debug_gizmos;
import simnet.game.shared;
import simnet.visualization;

export namespace simnet::visualization::detail {
    /// Returns the active sidebar rectangle.
    [[nodiscard]] Rectangle sidebar_rect() noexcept
    {
        const float width = std::clamp(static_cast<float>(GetScreenWidth()) * 0.20f, 280.0f, 420.0f);
        return Rectangle{0.0f, 0.0f, width, static_cast<float>(GetScreenHeight())};
    }

    /// Returns the collapsed overlay hint rectangle.
    [[nodiscard]] Rectangle overlay_hint_rect() noexcept
    {
        return Rectangle{12.0f, 12.0f, 96.0f, 34.0f};
    }

    /// Returns true when point is inside rect.
    [[nodiscard]] bool point_in_rect(Vector2 point, Rectangle rect) noexcept
    {
        return point.x >= rect.x && point.x <= rect.x + rect.width &&
               point.y >= rect.y && point.y <= rect.y + rect.height;
    }

    /// Draws one text run using the embedded font.
    void draw_text(Font font, const char *text, float x, float y, float size, Color color)
    {
        DrawTextEx(font, text, Vector2{x, y}, size, 1.0f, color);
    }

    /// Draws the collapsed overlay hint.
    void draw_overlay_hint(Font font)
    {
        const Rectangle rect = overlay_hint_rect();
        DrawRectangleRec(rect, Color{20, 24, 31, 220});
        DrawRectangleLinesEx(rect, 1.0f, Color{92, 105, 124, 180});
        draw_text(font, "O overlay", rect.x + 10.0f, rect.y + 9.0f, 16.0f, Color{205, 214, 224, 255});
    }

    /// Draws the left sidebar for one rendered frame.
    void draw_sidebar(Font font,
                      const char *role_label,
                      const SimulationViewFrame &frame,
                      const SimulationViewStats *stats,
                      const SpatialGridDebug *debug,
                      const SimulationRuleDebug *rules,
                      const game::shared::BoidViewInstance *tracked_boid,
                      const TrackedGizmoState &gizmos,
                      bool spatial_cells_visible,
                      std::optional<std::uint32_t> tracked_boid_id)
    {
        const Rectangle panel = sidebar_rect();
        DrawRectangleRec(panel, Color{18, 22, 28, 232});
        DrawRectangleLinesEx(panel, 1.0f, Color{92, 105, 124, 180});

        char line[192]{};
        const float x = panel.x + 18.0f;
        const float width = panel.width - 36.0f;
        float y = 20.0f;

        auto separator = [&]() {
            y += 8.0f;
            DrawLine(static_cast<int>(x),
                     static_cast<int>(y),
                     static_cast<int>(x + width),
                     static_cast<int>(y),
                     Color{92, 105, 124, 120});
            y += 14.0f;
        };

        auto section = [&](const char *title) {
            draw_text(font, title, x, y, 15.0f, Color{144, 188, 232, 255});
            y += 24.0f;
        };

        auto row = [&](const char *text, Color color = Color{210, 218, 228, 255}) {
            draw_text(font, text, x, y, 16.0f, color);
            y += 22.0f;
        };

        draw_text(font, "SimNetLab", x, y, 24.0f, RAYWHITE);
        y += 30.0f;
        draw_text(font, role_label, x, y, 16.0f, Color{164, 172, 184, 255});
        y += 26.0f;
        separator();

        section("Run");
        std::snprintf(line, sizeof(line), "render FPS %d", GetFPS());
        row(line, RAYWHITE);
        std::snprintf(line, sizeof(line), "sim tick %llu", static_cast<unsigned long long>(frame.tick));
        row(line);
        std::snprintf(line, sizeof(line), "boids %zu", frame.boids.size());
        row(line);
        if (stats != nullptr && stats->target_boids != frame.boids.size()) {
            std::snprintf(line, sizeof(line), "target %u", stats->target_boids);
            row(line);
        }
        separator();

        section("Flock");
        if (stats != nullptr) {
            std::snprintf(line, sizeof(line), "speed %.2f avg / %.2f max", stats->avg_speed, stats->max_speed);
            row(line);
            std::snprintf(line, sizeof(line), "steer %.3f avg", stats->avg_steer);
            row(line);
            std::snprintf(line, sizeof(line), "polarization %.3f", stats->polarization);
            row(line);
        } else {
            row("stats unavailable", Color{164, 172, 184, 255});
        }
        separator();

        section("Debug");
        std::snprintf(line,
                      sizeof(line),
                      "spatial cells %s%s",
                      spatial_cells_visible ? "on" : "off",
                      spatial_cells_visible && debug == nullptr ? " (no data)" : "");
        row(line, spatial_cells_visible ? Color{124, 214, 156, 255} : Color{180, 186, 196, 255});

        if (tracked_boid_id.has_value()) {
            std::snprintf(line, sizeof(line), "tracked id %u", *tracked_boid_id);
            row(line, tracked_boid != nullptr ? Color{255, 235, 120, 255} : Color{255, 160, 120, 255});
        } else {
            row("tracked none");
        }

        const char *mode = "global";
        Color mode_color{180, 186, 196, 255};
        if (tracked_boid_id.has_value()) {
            mode = tracked_boid != nullptr ? "tracking" : "tracking lost";
            mode_color = tracked_boid != nullptr ? Color{255, 235, 120, 255} : Color{255, 160, 120, 255};
        }
        std::snprintf(line, sizeof(line), "mode %s", mode);
        row(line, mode_color);
        if (tracked_boid_id.has_value()) {
            std::snprintf(line,
                          sizeof(line),
                          "gizmos 2:%s 3:%s 4:%s",
                          gizmos.trail ? "on" : "off",
                          gizmos.body ? "on" : "off",
                          gizmos.separation ? "on" : "off");
            row(line, Color{164, 172, 184, 255});
            std::snprintf(line,
                          sizeof(line),
                          "       5:%s 6:%s 7:%s 8:%s",
                          gizmos.alignment ? "on" : "off",
                          gizmos.cohesion ? "on" : "off",
                          gizmos.fov ? "on" : "off",
                          gizmos.query_cells ? "on" : "off");
            row(line, Color{164, 172, 184, 255});
        }

        if (tracked_boid != nullptr) {
            separator();
            section("Tracked Boid");
            std::snprintf(line, sizeof(line), "id %u", tracked_boid->id);
            row(line, Color{255, 235, 120, 255});
            std::snprintf(line,
                          sizeof(line),
                          "pos %.1f x / %.1f y / %.1f z",
                          tracked_boid->position.x(),
                          tracked_boid->position.y(),
                          tracked_boid->position.z());
            row(line);
            std::snprintf(line,
                          sizeof(line),
                          "head %.2f x / %.2f y / %.2f z",
                          tracked_boid->heading.x(),
                          tracked_boid->heading.y(),
                          tracked_boid->heading.z());
            row(line);
            std::snprintf(line, sizeof(line), "hue %u", tracked_boid->hue);
            row(line);
            if (rules != nullptr) {
                std::snprintf(line, sizeof(line), "separation radius %.1f", rules->separation_radius);
                row(line, Color{230, 128, 128, 255});
                std::snprintf(line, sizeof(line), "alignment radius %.1f", rules->local_alignment_radius);
                row(line, Color{128, 190, 245, 255});
                std::snprintf(line, sizeof(line), "cohesion radius %.1f", rules->cohesion_radius);
                row(line, Color{145, 220, 170, 255});
                std::snprintf(line, sizeof(line), "FOV cosine %.2f", rules->fov_cos);
                row(line, Color{255, 220, 140, 255});
            }
        }

        separator();
        section("Controls");
        row("Right drag orbit");
        row("Wheel zoom");
        row("R reset");
        row("1 spatial cells");
        row("2 trail");
        row("3 marker/body");
        row("4 separation");
        row("5 alignment");
        row("6 cohesion");
        row("7 FOV");
        row("8 query cells");
        row("O overlay");
        row("Click boid track");
        row("[ ] prev/next");
    }
}

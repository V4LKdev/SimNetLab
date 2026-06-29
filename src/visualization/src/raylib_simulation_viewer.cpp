module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <raylib.h>
#include <raymath.h>

#include "font_jetbrains.h"
#include "telemetry/trace.hpp"

module simnet.visualization;

import :raylib_boid_transform;
import :raylib_debug_gizmos;
import :raylib_instancing;
import :raylib_viewer_sidebar;
import simnet.game.shared;
import simnet.telemetry;
import simnet.utils;

namespace simnet::visualization {
    namespace {
        constexpr int hue_bucket_count = 32;
        constexpr float boid_radius = 0.22f;
        constexpr float boid_model_scale = 0.05f;
        constexpr float min_camera_distance = 4.0f;
        constexpr float default_camera_yaw = PI * 0.25f;
        constexpr float default_camera_pitch = PI / 6.0f;
        constexpr float max_pitch = PI / 2.0f - 0.04f;
        constexpr float pick_radius = 2.0f;
        constexpr float tracked_camera_distance = 48.0f;
        constexpr std::size_t tracked_trail_max_points = 2400;
        constexpr float tracked_trail_min_distance_sq = 0.15f * 0.15f;

        [[nodiscard]] Vector3 to_raylib(const utils::Vec3 &value) noexcept
        {
            return Vector3{value.x(), value.y(), value.z()};
        }

        [[nodiscard]] Color hue_color(std::uint8_t hue) noexcept
        {
            return ColorFromHSV(static_cast<float>(hue) * (360.0f / 256.0f), 0.72f, 0.96f);
        }

        [[nodiscard]] std::size_t hue_bucket(std::uint8_t hue) noexcept
        {
            return static_cast<std::size_t>(hue) * hue_bucket_count / 256;
        }

        struct ViewerUiState {
            bool overlay_visible = true;
            bool hovered = false;
        };

        struct ViewerDebugState {
            bool spatial_cells_visible = false;
            std::optional<std::uint32_t> tracked_boid_id;
        };

        [[nodiscard]] std::optional<float> ray_sphere_hit_distance(Ray ray, Vector3 center, float radius) noexcept
        {
            const Vector3 oc = Vector3Subtract(ray.position, center);
            const float b = Vector3DotProduct(oc, ray.direction);
            const float c = Vector3LengthSqr(oc) - radius * radius;
            const float discriminant = b * b - c;
            if (discriminant < 0.0f) {
                return std::nullopt;
            }

            const float root = std::sqrt(discriminant);
            float t = -b - root;
            if (t < 0.0f) {
                t = -b + root;
            }
            if (t < 0.0f) {
                return std::nullopt;
            }
            return t;
        }

        [[nodiscard]] std::optional<std::uint32_t> pick_boid_id(
            Ray ray,
            std::span<const game::shared::BoidViewInstance> boids) noexcept
        {
            std::optional<std::uint32_t> picked;
            float nearest = std::numeric_limits<float>::max();
            for (const auto &boid: boids) {
                const auto hit = ray_sphere_hit_distance(ray, to_raylib(boid.position), pick_radius);
                if (hit.has_value() && *hit < nearest) {
                    nearest = *hit;
                    picked = boid.id;
                }
            }
            return picked;
        }

        [[nodiscard]] const game::shared::BoidViewInstance *find_tracked_boid(
            std::span<const game::shared::BoidViewInstance> boids,
            std::optional<std::uint32_t> id) noexcept
        {
            if (!id.has_value()) {
                return nullptr;
            }
            for (const auto &boid: boids) {
                if (boid.id == *id) {
                    return &boid;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<std::size_t> find_boid_index(
            std::span<const game::shared::BoidViewInstance> boids,
            std::uint32_t id) noexcept
        {
            for (std::size_t index = 0; index < boids.size(); ++index) {
                if (boids[index].id == id) {
                    return index;
                }
            }
            return std::nullopt;
        }
    }

    class RaylibSimulationViewer::Impl {
    public:
        explicit Impl(VisualizerConfig config)
            : config_(std::move(config))
        {
            SetTraceLogLevel(LOG_WARNING);
            SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
            InitWindow(config_.width, config_.height, config_.title.c_str());
            if (config_.target_fps > 0) {
                SetTargetFPS(config_.target_fps);
            }

            font_ = LoadFontFromMemory(
                ".ttf",
                JetBrainsMono_Regular_ttf,
                static_cast<int>(JetBrainsMono_Regular_ttf_len),
                18,
                nullptr,
                0);
            SetTextureFilter(font_.texture, TEXTURE_FILTER_BILINEAR);

            boid_assets_ = detail::load_boid_render_assets();

            camera_.up = Vector3{0.0f, 1.0f, 0.0f};
            camera_.fovy = 55.0f;
            camera_.projection = CAMERA_PERSPECTIVE;
        }

        ~Impl()
        {
            detail::unload_boid_render_assets(boid_assets_);
            if (font_.texture.id != 0) {
                UnloadFont(font_);
            }
            if (IsWindowReady()) {
                CloseWindow();
            }
        }

        [[nodiscard]] bool should_close() const noexcept
        {
            return WindowShouldClose();
        }

        [[nodiscard]] bool wants_spatial_debug() const noexcept
        {
            return debug_.spatial_cells_visible;
        }

        void draw(const SimulationViewFrame &frame,
                  const SimulationViewStats *stats,
                  const SimulationDebugFrame *debug)
        {
            SIMNET_TRACE_ZONE_C("Render_Frame", simnet::telemetry::trace_color::rendering);
            const SpatialGridDebug *spatial_debug = debug != nullptr ? debug->spatial_grid : nullptr;
            const SimulationRuleDebug *rule_debug = debug != nullptr ? debug->rules : nullptr;

            {
                SIMNET_TRACE_ZONE_C("Render_UpdateViewerState", simnet::telemetry::trace_color::rendering);
                initialize_camera(frame.world_half);
                update_ui();
                update_toggles(frame.world_half);
                update_debug_selection(frame.boids);
            }

            const auto *tracked_boid = find_tracked_boid(frame.boids, debug_.tracked_boid_id);
            update_tracked_trail(tracked_boid);
            {
                SIMNET_TRACE_ZONE_C("Render_UpdateCamera", simnet::telemetry::trace_color::rendering);
                update_camera(tracked_boid != nullptr ? to_raylib(tracked_boid->position) : Vector3{0.0f, 0.0f, 0.0f});
            }

            {
                SIMNET_TRACE_ZONE_C("Render_ClearTransformBuckets", simnet::telemetry::trace_color::rendering);
                clear_transform_buckets();
            }

            {
                SIMNET_TRACE_ZONE_C("Render_ReserveTransformBuckets", simnet::telemetry::trace_color::rendering);
                reserve_transform_buckets(frame.boids.size());
            }

            {
                SIMNET_TRACE_ZONE_C("Render_BuildInstanceTransforms", simnet::telemetry::trace_color::rendering);

                for (const auto &boid: frame.boids) {
                    transform_buckets_[hue_bucket(boid.hue)].push_back(
                        detail::basic_boid_transform(boid, boid_model_scale));
                }
            }

            SIMNET_TRACE_PLOT("render_boids", static_cast<double>(frame.boids.size()));

            BeginDrawing();
            ClearBackground(Color{14, 16, 20, 255});

            BeginMode3D(camera_);

            {
                SIMNET_TRACE_ZONE_C("Render_DrawWorld", simnet::telemetry::trace_color::rendering);
                draw_world(frame.world_half);
            }

            if (debug_.spatial_cells_visible && spatial_debug != nullptr) {
                SIMNET_TRACE_ZONE_C("Render_DrawSpatialCells", simnet::telemetry::trace_color::rendering);
                detail::draw_spatial_cells(*spatial_debug);
            }

            {
                SIMNET_TRACE_ZONE_C("Render_DrawBoids", simnet::telemetry::trace_color::rendering);
                draw_boids(frame.boids);
            }

            if (tracked_boid != nullptr) {
                SIMNET_TRACE_ZONE_C("Render_DrawTrackedBoid", simnet::telemetry::trace_color::rendering);
                if (gizmos_.trail) {
                    detail::draw_tracked_path(tracked_trail_);
                }
                if (gizmos_.body) {
                    detail::draw_tracked_boid_marker(*tracked_boid, boid_radius);
                }
                if (rule_debug != nullptr) {
                    detail::draw_tracked_rule_gizmos(*tracked_boid, *rule_debug, boid_radius, gizmos_);
                    if (gizmos_.query_cells && debug_.spatial_cells_visible && spatial_debug != nullptr) {
                        detail::draw_tracked_query_cells(*tracked_boid, *spatial_debug, *rule_debug);
                    }
                }
            }

            EndMode3D();

            if (ui_.overlay_visible) {
                SIMNET_TRACE_ZONE_C("Render_DrawOverlay", simnet::telemetry::trace_color::rendering);
                detail::draw_sidebar(font_,
                                     config_.role_label.c_str(),
                                     frame,
                                     stats,
                                     spatial_debug,
                                     rule_debug,
                                     tracked_boid,
                                     gizmos_,
                                     debug_.spatial_cells_visible,
                                     debug_.tracked_boid_id);
            } else {
                detail::draw_overlay_hint(font_);
            }

            EndDrawing();
            SIMNET_TRACE_FRAME("RenderFrame");
        }

    private:
        void initialize_camera(float world_half)
        {
            if (camera_initialized_) {
                return;
            }

            camera_distance_ = std::max(world_half * 3.0f, min_camera_distance);
            max_camera_distance_ = std::max(camera_distance_ * 3.0f, 100.0f);
            camera_initialized_ = true;
            update_camera_position(Vector3{0.0f, 0.0f, 0.0f});
        }

        void reset_global_camera(float world_half)
        {
            camera_yaw_ = default_camera_yaw;
            camera_pitch_ = default_camera_pitch;
            camera_distance_ = std::max(world_half * 3.0f, min_camera_distance);
            max_camera_distance_ = std::max(camera_distance_ * 3.0f, 100.0f);
        }

        void update_ui()
        {
            const Rectangle rect = ui_.overlay_visible ? detail::sidebar_rect() : detail::overlay_hint_rect();
            ui_.hovered = detail::point_in_rect(GetMousePosition(), rect);
        }

        void update_toggles(float world_half)
        {
            if (IsKeyPressed(KEY_R)) {
                debug_.tracked_boid_id.reset();
                tracked_trail_.clear();
                reset_global_camera(world_half);
            }

            if (IsKeyPressed(KEY_ONE)) {
                debug_.spatial_cells_visible = !debug_.spatial_cells_visible;
            }

            if (IsKeyPressed(KEY_O)) {
                ui_.overlay_visible = !ui_.overlay_visible;
            }

            if (!debug_.tracked_boid_id.has_value()) {
                return;
            }

            if (IsKeyPressed(KEY_TWO)) {
                gizmos_.trail = !gizmos_.trail;
            }
            if (IsKeyPressed(KEY_THREE)) {
                gizmos_.body = !gizmos_.body;
            }
            if (IsKeyPressed(KEY_FOUR)) {
                gizmos_.separation = !gizmos_.separation;
            }
            if (IsKeyPressed(KEY_FIVE)) {
                gizmos_.alignment = !gizmos_.alignment;
            }
            if (IsKeyPressed(KEY_SIX)) {
                gizmos_.cohesion = !gizmos_.cohesion;
            }
            if (IsKeyPressed(KEY_SEVEN)) {
                gizmos_.fov = !gizmos_.fov;
            }
            if (IsKeyPressed(KEY_EIGHT)) {
                gizmos_.query_cells = !gizmos_.query_cells;
            }
        }

        void update_debug_selection(std::span<const game::shared::BoidViewInstance> boids)
        {
            if (!ui_.hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                const auto picked = pick_boid_id(GetMouseRay(GetMousePosition(), camera_), boids);
                if (picked.has_value()) {
                    set_tracked_boid(*picked);
                    camera_distance_ = tracked_camera_distance;
                }
            }
            if (debug_.tracked_boid_id.has_value() && !boids.empty()) {
                const auto index = find_boid_index(boids, *debug_.tracked_boid_id);
                if (index.has_value()) {
                    if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
                        set_tracked_boid(boids[(*index + 1U) % boids.size()].id);
                        // Do not reset distance when hopping from one boid to another.
                        // camera_distance_ = tracked_camera_distance;
                    }
                    if (IsKeyPressed(KEY_LEFT_BRACKET)) {
                        set_tracked_boid(boids[(*index + boids.size() - 1U) % boids.size()].id);
                        // camera_distance_ = tracked_camera_distance;
                    }
                }
            }
        }

        void set_tracked_boid(std::uint32_t id)
        {
            if (debug_.tracked_boid_id != id) {
                tracked_trail_.clear();
            }
            debug_.tracked_boid_id = id;
        }

        void update_tracked_trail(const game::shared::BoidViewInstance *tracked_boid)
        {
            if (tracked_boid == nullptr) {
                return;
            }

            const Vector3 position = to_raylib(tracked_boid->position);
            if (!tracked_trail_.empty() &&
                Vector3DistanceSqr(tracked_trail_.back(), position) < tracked_trail_min_distance_sq) {
                return;
            }

            tracked_trail_.push_back(position);
            if (tracked_trail_.size() > tracked_trail_max_points) {
                tracked_trail_.erase(tracked_trail_.begin());
            }
        }

        void update_camera(Vector3 target)
        {
            if (!ui_.hovered && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                const Vector2 delta = GetMouseDelta();
                camera_yaw_ -= delta.x * 0.006f;
                camera_pitch_ = std::clamp(camera_pitch_ + delta.y * 0.006f, -max_pitch, max_pitch);
            }
            if (!ui_.hovered) {
                const float wheel = GetMouseWheelMove();
                if (wheel != 0.0f) {
                    camera_distance_ = std::clamp(
                        camera_distance_ * (1.0f - wheel * 0.10f),
                        min_camera_distance,
                        max_camera_distance_);
                }
            }
            update_camera_position(target);
        }


        void update_camera_position(Vector3 target)
        {
            const float cp = std::cos(camera_pitch_);
            camera_.target = target;
            camera_.position = Vector3{
                target.x + camera_distance_ * cp * std::sin(camera_yaw_),
                target.y + camera_distance_ * std::sin(camera_pitch_),
                target.z + camera_distance_ * cp * std::cos(camera_yaw_)
            };
        }


        void clear_transform_buckets()
        {
            for (auto &bucket: transform_buckets_) {
                bucket.clear();
            }
        }

        void reserve_transform_buckets(std::size_t boid_count)
        {
            const std::size_t reserve_count = boid_count / transform_buckets_.size() + 16;
            for (auto &bucket: transform_buckets_) {
                if (bucket.capacity() < reserve_count) {
                    bucket.reserve(reserve_count);
                }
            }
        }

        static void draw_world(float world_half)
        {
            DrawCubeWires(Vector3{0.0f, 0.0f, 0.0f},
                          world_half * 2.0f,
                          world_half * 2.0f,
                          world_half * 2.0f,
                          Color{86, 102, 122, 160});
        }

        void draw_boids(std::span<const game::shared::BoidViewInstance> boids) const
        {
            if (boid_assets_.loaded) {
                std::size_t draw_calls = 0;
                std::size_t drawn_transforms = 0;
                for (std::size_t bucket_index = 0; bucket_index < transform_buckets_.size(); ++bucket_index) {
                    auto &bucket = transform_buckets_[bucket_index];
                    if (bucket.empty()) {
                        continue;
                    }
                    ++draw_calls;
                    drawn_transforms += bucket.size();
                    boid_assets_.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color =
                            hue_color(static_cast<std::uint8_t>(bucket_index * 256 / transform_buckets_.size()));
                    DrawMeshInstanced(boid_assets_.model.meshes[0],
                                      boid_assets_.model.materials[0],
                                      bucket.data(),
                                      static_cast<int>(bucket.size()));
                }
                SIMNET_TRACE_PLOT("render_instanced_draw_calls", static_cast<double>(draw_calls));
                SIMNET_TRACE_PLOT("render_instanced_avg_transforms",
                                  draw_calls > 0 ? static_cast<double>(drawn_transforms) / draw_calls : 0.0);
                return;
            }

            for (const auto &boid: boids) {
                DrawSphere(to_raylib(boid.position), boid_radius * 1.6f, hue_color(boid.hue));
            }
        }

        VisualizerConfig config_;
        Camera3D camera_{};
        Font font_{};
        detail::BoidRenderAssets boid_assets_{};
        ViewerUiState ui_;
        ViewerDebugState debug_;
        detail::TrackedGizmoState gizmos_;
        bool camera_initialized_ = false;
        float camera_yaw_ = default_camera_yaw;
        float camera_pitch_ = default_camera_pitch;
        float camera_distance_ = 30.0f;
        float max_camera_distance_ = 100.0f;
        std::array<std::vector<Matrix>, hue_bucket_count> transform_buckets_;
        std::vector<Vector3> tracked_trail_;
    };

    RaylibSimulationViewer::RaylibSimulationViewer(VisualizerConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RaylibSimulationViewer::~RaylibSimulationViewer() = default;

    RaylibSimulationViewer::RaylibSimulationViewer(RaylibSimulationViewer &&) noexcept = default;

    RaylibSimulationViewer &RaylibSimulationViewer::operator=(RaylibSimulationViewer &&) noexcept = default;

    bool RaylibSimulationViewer::should_close() const noexcept
    {
        return impl_->should_close();
    }

    bool RaylibSimulationViewer::wants_spatial_debug() const noexcept
    {
        return impl_->wants_spatial_debug();
    }

    void RaylibSimulationViewer::draw(const SimulationViewFrame &frame,
                                      const SimulationViewStats *stats,
                                      const SimulationDebugFrame *debug) const
    {
        impl_->draw(frame, stats, debug);
    }
}

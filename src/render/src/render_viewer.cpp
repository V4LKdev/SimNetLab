module;

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <simnet/telemetry_trace.hpp>

#include "../assets/jetbrains_mono_regular.hpp"

module simnet.render;

import simnet.telemetry;

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr std::size_t hue_bucket_count = 32;
    constexpr float pi = 3.14159265358979323846F;
    constexpr float min_pitch = -pi * 0.48F;
    constexpr float max_pitch = pi * 0.48F;
    constexpr float minimum_distance = 2.0F;

    bool viewer_active = false;

    struct SceneRect
    {
        int x {};
        int y {};
        int width {};
        int height {};
    };

    [[nodiscard]] simnet::NS elapsed_ns(Clock::time_point start) noexcept
    {
        return std::chrono::duration_cast<simnet::NS>(Clock::now() - start);
    }

    void validate_config(simnet::ViewerConfig const& config)
    {
        if (config.window_width == 0 || config.window_height == 0) {
            throw std::runtime_error("viewer window dimensions must be non-zero");
        }
        if (config.panel_width >= config.window_width) {
            throw std::runtime_error("viewer panel_width must be less than window_width");
        }
        if (config.target_frame_rate == 0) {
            throw std::runtime_error("viewer target_frame_rate must be non-zero");
        }
        if (config.entity_scale <= 0.0F || config.picking_radius <= 0.0F) {
            throw std::runtime_error("viewer entity_scale and picking_radius must be positive");
        }
        if (config.stationary_observer_interest_radius <= 0.0F
            || config.stationary_observer_vertical_fov_degrees <= 0.0F
            || config.stationary_observer_vertical_fov_degrees >= 180.0F
            || config.max_visible_spatial_cells == 0U) {
            throw std::runtime_error("viewer observer and spatial settings are invalid");
        }
    }

    [[nodiscard]] Vector3 to_raylib(simnet::Vec3f value) noexcept
    {
        return { value.x, value.y, value.z };
    }

    [[nodiscard]] Color to_raylib(simnet::DebugColor value) noexcept
    {
        return { value.red, value.green, value.blue, value.alpha };
    }

    [[nodiscard]] bool finite(simnet::Vec3f value) noexcept
    {
        return simnet::is_finite(value);
    }

    [[nodiscard]] simnet::Vec3f normalized_or_forward(simnet::Vec3f heading) noexcept
    {
        auto const length_squared = simnet::length_squared(heading);
        if (!std::isfinite(length_squared) || length_squared <= 0.000001F) {
            return { 0.0F, 0.0F, 1.0F };
        }
        return heading / std::sqrt(length_squared);
    }

    [[nodiscard]] simnet::Vec3f cross(simnet::Vec3f lhs, simnet::Vec3f rhs) noexcept
    {
        return {
            .x = lhs.y * rhs.z - lhs.z * rhs.y,
            .y = lhs.z * rhs.x - lhs.x * rhs.z,
            .z = lhs.x * rhs.y - lhs.y * rhs.x,
        };
    }

    struct WorldUpBasis
    {
        simnet::Vec3f right { .x = 1.0F };
        simnet::Vec3f up { .y = 1.0F };
    };

    [[nodiscard]] WorldUpBasis world_up_basis(simnet::Vec3f forward) noexcept
    {
        auto const horizontal = simnet::normalize_or(
            simnet::Vec3f { .x = forward.x, .z = forward.z },
            simnet::Vec3f { .z = 1.0F }
        );
        auto const right = simnet::normalize_or(
            cross(simnet::Vec3f { .y = 1.0F }, horizontal),
            simnet::Vec3f { .x = 1.0F }
        );
        return {
            .right = right,
            .up = simnet::normalize_or(
                cross(forward, right),
                simnet::Vec3f { .y = 1.0F }
            ),
        };
    }

    [[nodiscard]] Matrix entity_transform(simnet::Vec3f position, simnet::Vec3f heading, float scale) noexcept
    {
        auto const forward = normalized_or_forward(heading);
        auto const sign = std::copysign(1.0F, forward.z);
        auto const a = -1.0F / (sign + forward.z);
        auto const b = forward.x * forward.y * a;
        auto const right = simnet::Vec3f {
            .x = 1.0F + sign * forward.x * forward.x * a,
            .y = sign * b,
            .z = -sign * forward.x,
        };
        auto const up = simnet::Vec3f {
            .x = b,
            .y = sign + forward.y * forward.y * a,
            .z = -forward.y,
        };

        return {
            .m0 = right.x * scale,
            .m4 = up.x * scale,
            .m8 = forward.x * scale,
            .m12 = position.x,
            .m1 = right.y * scale,
            .m5 = up.y * scale,
            .m9 = forward.y * scale,
            .m13 = position.y,
            .m2 = right.z * scale,
            .m6 = up.z * scale,
            .m10 = forward.z * scale,
            .m14 = position.z,
            .m3 = 0.0F,
            .m7 = 0.0F,
            .m11 = 0.0F,
            .m15 = 1.0F,
        };
    }

    [[nodiscard]] Color hue_color(std::uint8_t hue) noexcept
    {
        return ColorFromHSV(static_cast<float>(hue) * (360.0F / 256.0F), 0.72F, 0.96F);
    }

    [[nodiscard]] std::size_t hue_bucket(std::uint8_t hue) noexcept
    {
        return static_cast<std::size_t>(hue) * hue_bucket_count / 256U;
    }

    [[nodiscard]] Mesh make_directional_mesh()
    {
        // The wedge points along local +Z and uses local +Y as up.
        auto mesh = Mesh {};
        mesh.vertexCount = 4;
        mesh.triangleCount = 4;
        mesh.vertices = static_cast<float*>(MemAlloc(sizeof(float) * 12));
        mesh.normals = static_cast<float*>(MemAlloc(sizeof(float) * 12));
        mesh.texcoords = static_cast<float*>(MemAlloc(sizeof(float) * 8));
        mesh.indices = static_cast<unsigned short*>(MemAlloc(sizeof(unsigned short) * 12));
        if (mesh.vertices == nullptr || mesh.normals == nullptr || mesh.texcoords == nullptr || mesh.indices == nullptr) {
            throw std::runtime_error("failed to allocate directional entity mesh");
        }

        float const vertices[] = {
            0.0F, 0.0F, 1.25F,
            -0.45F, -0.25F, -0.65F,
            0.45F, -0.25F, -0.65F,
            0.0F, 0.45F, -0.35F,
        };
        float const normals[] = {
            0.0F, 0.0F, 1.0F,
            -0.5F, -0.3F, -0.8F,
            0.5F, -0.3F, -0.8F,
            0.0F, 0.8F, -0.5F,
        };
        float const texcoords[] = { 0.5F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.5F, 0.5F };
        unsigned short const indices[] = { 0, 1, 2, 0, 2, 3, 0, 3, 1, 1, 3, 2 };
        std::copy(std::begin(vertices), std::end(vertices), mesh.vertices);
        std::copy(std::begin(normals), std::end(normals), mesh.normals);
        std::copy(std::begin(texcoords), std::end(texcoords), mesh.texcoords);
        std::copy(std::begin(indices), std::end(indices), mesh.indices);
        UploadMesh(&mesh, false);
        return mesh;
    }

    [[nodiscard]] Matrix mesh_correction(std::string const& path) noexcept
    {
        if (std::filesystem::path { path }.filename() == "boid.obj") {
            // The reference boid already uses local +Z as forward and centimeter-sized coordinates.
            return MatrixScale(0.05F, 0.05F, 0.05F);
        }
        return MatrixIdentity();
    }

    void bake_mesh_correction(Mesh& mesh, Matrix correction)
    {
        if (mesh.vertices == nullptr || mesh.vertexCount <= 0) {
            return;
        }
        for (auto index = 0; index < mesh.vertexCount; ++index) {
            auto const offset = index * 3;
            auto const corrected = Vector3Transform(
                { mesh.vertices[offset], mesh.vertices[offset + 1], mesh.vertices[offset + 2] },
                correction
            );
            mesh.vertices[offset] = corrected.x;
            mesh.vertices[offset + 1] = corrected.y;
            mesh.vertices[offset + 2] = corrected.z;
        }
        UpdateMeshBuffer(mesh, 0, mesh.vertices, mesh.vertexCount * 3 * static_cast<int>(sizeof(float)), 0);

        if (mesh.normals == nullptr) {
            return;
        }
        for (auto index = 0; index < mesh.vertexCount; ++index) {
            auto const offset = index * 3;
            auto const normal = Vector3 {
                correction.m0 * mesh.normals[offset] + correction.m4 * mesh.normals[offset + 1]
                    + correction.m8 * mesh.normals[offset + 2],
                correction.m1 * mesh.normals[offset] + correction.m5 * mesh.normals[offset + 1]
                    + correction.m9 * mesh.normals[offset + 2],
                correction.m2 * mesh.normals[offset] + correction.m6 * mesh.normals[offset + 1]
                    + correction.m10 * mesh.normals[offset + 2],
            };
            auto const corrected = Vector3Normalize(normal);
            mesh.normals[offset] = corrected.x;
            mesh.normals[offset + 1] = corrected.y;
            mesh.normals[offset + 2] = corrected.z;
        }
        UpdateMeshBuffer(mesh, 2, mesh.normals, mesh.vertexCount * 3 * static_cast<int>(sizeof(float)), 0);
    }

    constexpr char const* instancing_vertex_shader = R"glsl(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in mat4 instanceTransform;
uniform mat4 mvp;
out vec4 fragColor;
out vec3 fragNormal;
void main()
{
    fragColor = vertexColor;
    fragNormal = normalize(mat3(instanceTransform) * vertexNormal);
    gl_Position = mvp * instanceTransform * vec4(vertexPosition, 1.0);
}
)glsl";

    constexpr char const* instancing_fragment_shader = R"glsl(
#version 330
in vec4 fragColor;
in vec3 fragNormal;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main()
{
    vec3 light_direction = normalize(vec3(0.35, 0.75, 0.45));
    float light = clamp(dot(normalize(fragNormal), light_direction), 0.0, 1.0) * 0.55 + 0.45;
    finalColor = vec4(colDiffuse.rgb * fragColor.rgb * light, colDiffuse.a);
}
)glsl";
}

namespace simnet
{
    class Viewer::Impl
    {
    public:
        explicit Impl(ViewerConfig config)
            : config_(std::move(config)),
              scene_rect_ {
                  .x = static_cast<int>(config_.panel_width),
                  .y = 0,
                  .width = static_cast<int>(config_.window_width - config_.panel_width),
                  .height = static_cast<int>(config_.window_height),
              }
        {
            validate_config(config_);
            if (viewer_active) {
                throw std::runtime_error("only one Viewer may be active per process");
            }
            SetTraceLogLevel(LOG_WARNING);
            InitWindow(static_cast<int>(config_.window_width), static_cast<int>(config_.window_height), config_.title.c_str());
            if (!IsWindowReady()) {
                throw std::runtime_error("failed to create viewer window");
            }
            font_ = LoadFontFromMemory(
                ".ttf",
                JetBrainsMono_Regular_ttf,
                static_cast<int>(JetBrainsMono_Regular_ttf_len),
                32,
                nullptr,
                0
            );
            if (font_.texture.id == 0) {
                CloseWindow();
                throw std::runtime_error("failed to load viewer font");
            }
            SetTextureFilter(font_.texture, TEXTURE_FILTER_BILINEAR);
            SetTargetFPS(static_cast<int>(config_.target_frame_rate));
            scene_ = LoadRenderTexture(scene_rect_.width, scene_rect_.height);
            if (scene_.texture.id == 0) {
                CloseWindow();
                throw std::runtime_error("failed to create viewer scene render texture");
            }
            load_entity_model();
            shader_ = LoadShaderFromMemory(instancing_vertex_shader, instancing_fragment_shader);
            instancing_available_ = shader_.id != 0
                && model_.meshCount > 0
                && model_.materialCount > 0
                && model_.meshes != nullptr
                && model_.materials != nullptr
                && model_.meshMaterial != nullptr;
            if (instancing_available_) {
                shader_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader_, "mvp");
                shader_.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(shader_, "colDiffuse");
                for (int index = 0; index < model_.materialCount; ++index) {
                    model_.materials[index].shader = shader_;
                }
            } else {
                TraceLog(LOG_WARNING, "SimNet viewer instanced entity drawing is unavailable");
            }
            camera_.up = { 0.0F, 1.0F, 0.0F };
            camera_.fovy = 55.0F;
            camera_.projection = CAMERA_PERSPECTIVE;
            viewer_active = true;
        }

        ~Impl()
        {
            if (model_.meshCount > 0) {
                UnloadModel(model_);
            }
            if (shader_.id != 0) {
                UnloadShader(shader_);
            }
            if (scene_.texture.id != 0) {
                UnloadRenderTexture(scene_);
            }
            if (font_.texture.id != 0) {
                UnloadFont(font_);
            }
            if (IsWindowReady()) {
                CloseWindow();
            }
            viewer_active = false;
        }

        [[nodiscard]] ViewerResult draw(RenderFrame const& frame)
        {
            SIMNET_TRACE_SCOPE_CATEGORY("render.viewer_frame", simnet::LogCategory::Render);
            auto result = ViewerResult {};
            auto stats = RenderStats {};
            auto const input_start = Clock::now();
            {
                SIMNET_TRACE_SCOPE_CATEGORY("render.input", simnet::LogCategory::Render);
                update_panel_input();
                update_camera(frame, result);
                update_selection(frame.entities, result);
                update_controls(frame, result);
            }
            stats.input_cpu_time = elapsed_ns(input_start);

            bool const valid_entities = frame.entities.valid();
            auto const preparation_start = Clock::now();
            {
                SIMNET_TRACE_SCOPE_CATEGORY("render.instance_preparation", simnet::LogCategory::Render);
                if (valid_entities) {
                    prepare_instances(frame.entities, stats);
                } else {
                    clear_instances();
                }
            }
            stats.preparation_cpu_time = elapsed_ns(preparation_start);

            auto const scene_start = Clock::now();
            {
                SIMNET_TRACE_SCOPE_CATEGORY("render.scene_submission", simnet::LogCategory::Render);
                draw_scene(frame, stats);
            }
            stats.scene_submit_cpu_time = elapsed_ns(scene_start);

            BeginDrawing();
            ClearBackground(Color { 18, 21, 27, 255 });
            auto const panel_start = Clock::now();
            {
                SIMNET_TRACE_SCOPE_CATEGORY("render.panel", simnet::LogCategory::Render);
                draw_panel(frame, valid_entities, stats, result);
            }
            stats.panel_cpu_time = elapsed_ns(panel_start);
            DrawTextureRec(
                scene_.texture,
                Rectangle { 0.0F, 0.0F, static_cast<float>(scene_rect_.width), -static_cast<float>(scene_rect_.height) },
                Vector2 { static_cast<float>(scene_rect_.x), static_cast<float>(scene_rect_.y) },
                WHITE
            );
            draw_help_overlay(frame);
            {
                SIMNET_TRACE_SCOPE_CATEGORY("render.present_wait", simnet::LogCategory::Render);
                EndDrawing();
            }

            result.close_requested = WindowShouldClose();
            result.view_mode = mode_;
            result.selected_entity = selected_entity_;
            result.stats = stats;
            SIMNET_TRACE_PLOT("render.instances", static_cast<double>(stats.instance_count));
            SIMNET_TRACE_PLOT("render.skipped_entities", static_cast<double>(stats.skipped_entity_count));
            SIMNET_TRACE_PLOT("render.draw_calls", static_cast<double>(stats.draw_calls));
            SIMNET_TRACE_PLOT("render.active_hue_buckets", static_cast<double>(stats.active_hue_buckets));
            if (frame.spatial.has_value()) {
                SIMNET_TRACE_PLOT("render.displayed_spatial_cells", static_cast<double>(frame.spatial->cells.size()));
            }
            return result;
        }

        void set_view_mode(ViewMode mode) noexcept
        {
            mode_ = mode == ViewMode::EntityDetail && !selected_entity_.has_value()
                ? ViewMode::Overview
                : mode;
        }

    private:
        void load_entity_model()
        {
            if (!config_.entity_mesh_path.empty()) {
                model_ = LoadModel(config_.entity_mesh_path.c_str());
                if (model_.meshCount > 0
                    && model_.materialCount > 0
                    && model_.meshes != nullptr
                    && model_.materials != nullptr
                    && model_.meshMaterial != nullptr) {
                    auto const correction = mesh_correction(config_.entity_mesh_path);
                    for (int index = 0; index < model_.meshCount; ++index) {
                        bake_mesh_correction(model_.meshes[index], correction);
                    }
                    return;
                }
                if (model_.meshCount > 0) {
                    UnloadModel(model_);
                    model_ = {};
                }
                TraceLog(LOG_WARNING, "SimNet viewer mesh load failed, using procedural wedge");
            }
            mesh_ = make_directional_mesh();
            model_ = LoadModelFromMesh(mesh_);
        }

        enum class PanelPage : std::uint8_t
        {
            Overview,
            Network,
            Entity
        };

        struct SelectedEntity
        {
            EntityNetId id {};
            Vec3f position {};
            Vec3f heading {};
            std::uint8_t hue {};
        };

        [[nodiscard]] std::optional<SelectedEntity> find_selected_entity(RenderEntityView const& entities) const
        {
            if (!selected_entity_.has_value() || !entities.valid()) {
                return std::nullopt;
            }
            for (std::size_t index = 0; index < entities.size(); ++index) {
                if (entities.ids[index] == *selected_entity_
                    && finite(entities.positions[index])
                    && finite(entities.headings[index])) {
                    return SelectedEntity {
                        .id = entities.ids[index],
                        .position = entities.positions[index],
                        .heading = entities.headings[index],
                        .hue = entities.hues[index],
                    };
                }
            }
            return std::nullopt;
        }

        void clear_selection(ViewerResult& result, bool preserve_navigation_anchor = false)
        {
            if (selected_entity_.has_value()) {
                if (preserve_navigation_anchor) {
                    navigation_anchor_ = selected_entity_;
                } else {
                    navigation_anchor_.reset();
                }
                selected_entity_.reset();
                result.selected_entity_changed = true;
            }
            selected_entity_frame_.reset();
            mode_ = ViewMode::Overview;
        }

        void update_camera(RenderFrame const& frame, ViewerResult& result)
        {
            auto const bounds = frame.info.world_bounds;
            auto const center = Vec3f {
                .x = (bounds.min.x + bounds.max.x) * 0.5F,
                .y = (bounds.min.y + bounds.max.y) * 0.5F,
                .z = (bounds.min.z + bounds.max.z) * 0.5F,
            };
            auto const extent = std::max({
                std::abs(bounds.max.x - bounds.min.x),
                std::abs(bounds.max.y - bounds.min.y),
                std::abs(bounds.max.z - bounds.min.z),
                1.0F,
            });
            min_distance_ = std::max(minimum_distance, extent * 0.05F);
            max_distance_ = std::max(min_distance_ * 2.0F, extent * 4.0F);
            if (!camera_initialized_) {
                reset_overview_camera(center);
                camera_initialized_ = true;
            }

            if (IsKeyPressed(KEY_F4)) {
                if (frame.game_camera.has_value()) {
                    mode_ = mode_ == ViewMode::Game ? ViewMode::Overview : ViewMode::Game;
                } else if (frame.stationary_observer.has_value()) {
                    mode_ = mode_ == ViewMode::StationaryObserver
                        ? ViewMode::Overview
                        : ViewMode::StationaryObserver;
                }
            }
            if (mode_ == ViewMode::Game && !frame.game_camera.has_value()) {
                mode_ = ViewMode::Overview;
            }
            if (mode_ == ViewMode::StationaryObserver
                && !frame.stationary_observer.has_value()) {
                mode_ = ViewMode::Overview;
            }
            if (frame.stationary_observer.has_value()) {
                result.stationary_observer_yaw_axis = (IsKeyDown(KEY_LEFT) ? 1.0F : 0.0F)
                    - (IsKeyDown(KEY_RIGHT) ? 1.0F : 0.0F);
                result.stationary_observer_pitch_axis = (IsKeyDown(KEY_UP) ? 1.0F : 0.0F)
                    - (IsKeyDown(KEY_DOWN) ? 1.0F : 0.0F);
            }

            selected_entity_frame_ = find_selected_entity(frame.entities);
            if (selected_entity_.has_value() && !selected_entity_frame_.has_value()) {
                clear_selection(result, true);
            }

            auto const mouse = GetMousePosition();
            auto const in_scene = mouse.x >= static_cast<float>(scene_rect_.x)
                && mouse.x < static_cast<float>(scene_rect_.x + scene_rect_.width)
                && mouse.y >= 0.0F
                && mouse.y < static_cast<float>(scene_rect_.height);
            if ((mode_ == ViewMode::Overview || mode_ == ViewMode::EntityDetail)
                && in_scene && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                auto const delta = GetMouseDelta();
                auto& yaw = mode_ == ViewMode::EntityDetail ? detail_yaw_ : overview_yaw_;
                auto& pitch = mode_ == ViewMode::EntityDetail ? detail_pitch_ : overview_pitch_;
                yaw -= delta.x * 0.006F;
                pitch = std::clamp(pitch + delta.y * 0.006F, min_pitch, max_pitch);
            }
            if ((mode_ == ViewMode::Overview || mode_ == ViewMode::EntityDetail) && in_scene) {
                auto const wheel = GetMouseWheelMove();
                if (wheel != 0.0F) {
                    auto& distance = mode_ == ViewMode::EntityDetail ? detail_distance_ : overview_distance_;
                    auto const minimum = mode_ == ViewMode::EntityDetail ? detail_min_distance_ : min_distance_;
                    auto const maximum = mode_ == ViewMode::EntityDetail ? detail_max_distance_ : max_distance_;
                    distance = std::clamp(distance * (1.0F - wheel * 0.1F), minimum, maximum);
                }
            }
            if (IsKeyPressed(KEY_F5)) {
                if (mode_ == ViewMode::EntityDetail && selected_entity_frame_.has_value()) {
                    reset_detail_camera(extent);
                } else {
                    reset_overview_camera(center);
                }
            }
            if (IsKeyPressed(KEY_BACKSPACE) && mode_ == ViewMode::EntityDetail) {
                clear_selection(result);
            }
            if (mode_ == ViewMode::Game && frame.game_camera.has_value()) {
                camera_.position = to_raylib(frame.game_camera->position);
                camera_.target = to_raylib(frame.game_camera->target);
                camera_.up = to_raylib(frame.game_camera->up);
                camera_.fovy = frame.game_camera->vertical_fov_degrees;
                result.player_input = {
                    .pitch_up = IsKeyDown(KEY_W),
                    .yaw_left = IsKeyDown(KEY_A),
                    .pitch_down = IsKeyDown(KEY_S),
                    .yaw_right = IsKeyDown(KEY_D),
                    .accelerate = IsKeyDown(KEY_LEFT_SHIFT)
                        || IsKeyDown(KEY_RIGHT_SHIFT),
                    .decelerate = IsKeyDown(KEY_LEFT_CONTROL)
                        || IsKeyDown(KEY_RIGHT_CONTROL),
                    .left_mouse = IsMouseButtonDown(MOUSE_BUTTON_LEFT),
                    .right_mouse = IsMouseButtonDown(MOUSE_BUTTON_RIGHT),
                };
            } else if (mode_ == ViewMode::StationaryObserver
                && frame.stationary_observer.has_value()) {
                auto const forward = normalized_or_forward(
                    frame.stationary_observer->forward
                );
                auto const basis = world_up_basis(forward);
                camera_.position = to_raylib(frame.stationary_observer->position);
                camera_.target = to_raylib(frame.stationary_observer->position + forward);
                camera_.up = to_raylib(basis.up);
                camera_.fovy = frame.stationary_observer->vertical_fov_degrees;
            } else if (mode_ == ViewMode::EntityDetail && selected_entity_frame_.has_value()) {
                target_ = to_raylib(selected_entity_frame_->position);
                detail_min_distance_ = std::max(0.5F, config_.entity_scale * 1.5F);
                detail_max_distance_ = std::max(detail_min_distance_ * 4.0F, extent * 0.5F);
                if (detail_distance_ <= 0.0F) {
                    reset_detail_camera(extent);
                }
                update_camera_position(detail_yaw_, detail_pitch_, detail_distance_);
            } else {
                target_ = to_raylib(center);
                update_camera_position(overview_yaw_, overview_pitch_, overview_distance_);
                camera_.fovy = 55.0F;
            }
        }

        void update_camera_position(float yaw, float pitch, float distance) noexcept
        {
            auto const cosine_pitch = std::cos(pitch);
            camera_.target = target_;
            camera_.position = {
                target_.x + distance * cosine_pitch * std::sin(yaw),
                target_.y + distance * std::sin(pitch),
                target_.z + distance * cosine_pitch * std::cos(yaw),
            };
        }

        void reset_overview_camera(Vec3f center) noexcept
        {
            target_ = to_raylib(center);
            overview_yaw_ = pi * 0.25F;
            overview_pitch_ = pi / 6.0F;
            overview_distance_ = std::clamp(max_distance_ * 0.45F, min_distance_, max_distance_);
        }

        void reset_detail_camera(float world_extent) noexcept
        {
            detail_yaw_ = pi * 0.25F;
            detail_pitch_ = pi / 6.0F;
            detail_distance_ = std::clamp(
                std::max(config_.entity_scale * 10.0F, world_extent * 0.03F),
                detail_min_distance_,
                detail_max_distance_
            );
        }

        [[nodiscard]] bool mouse_in_scene(Vector2 mouse) const noexcept
        {
            return mouse.x >= static_cast<float>(scene_rect_.x)
                && mouse.x < static_cast<float>(scene_rect_.x + scene_rect_.width)
                && mouse.y >= static_cast<float>(scene_rect_.y)
                && mouse.y < static_cast<float>(scene_rect_.y + scene_rect_.height);
        }

        [[nodiscard]] static std::optional<float> ray_sphere_hit_distance(
            Ray ray,
            Vector3 center,
            float radius
        ) noexcept
        {
            auto const offset = Vector3Subtract(ray.position, center);
            auto const projection = Vector3DotProduct(offset, ray.direction);
            auto const discriminant = projection * projection
                - (Vector3DotProduct(offset, offset) - radius * radius);
            if (discriminant < 0.0F) {
                return std::nullopt;
            }
            auto const root = std::sqrt(discriminant);
            auto const near_distance = -projection - root;
            auto const far_distance = -projection + root;
            if (near_distance >= 0.0F) {
                return near_distance;
            }
            if (far_distance >= 0.0F) {
                return far_distance;
            }
            return std::nullopt;
        }

        void update_selection(RenderEntityView const& entities, ViewerResult& result)
        {
            if (mode_ == ViewMode::Game) {
                return;
            }
            if (IsKeyPressed(KEY_LEFT_BRACKET)) {
                select_adjacent_entity(entities, -1, result);
                return;
            }
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
                select_adjacent_entity(entities, 1, result);
                return;
            }
            auto const mouse = GetMousePosition();
            if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || !mouse_in_scene(mouse) || !entities.valid()) {
                return;
            }
            auto const scene_mouse = Vector2 {
                mouse.x - static_cast<float>(scene_rect_.x),
                mouse.y - static_cast<float>(scene_rect_.y),
            };
            auto const ray = GetScreenToWorldRayEx(scene_mouse, camera_, scene_rect_.width, scene_rect_.height);
            auto nearest_distance = std::numeric_limits<float>::infinity();
            auto hit = std::optional<SelectedEntity> {};
            for (std::size_t index = 0; index < entities.size(); ++index) {
                auto const position = entities.positions[index];
                auto const heading = entities.headings[index];
                if (!finite(position) || !finite(heading)) {
                    continue;
                }
                auto const distance = ray_sphere_hit_distance(ray, to_raylib(position), config_.picking_radius);
                if (distance.has_value() && *distance < nearest_distance) {
                    nearest_distance = *distance;
                    hit = SelectedEntity {
                        .id = entities.ids[index],
                        .position = position,
                        .heading = heading,
                        .hue = entities.hues[index],
                    };
                }
            }
            if (!hit.has_value()) {
                return;
            }
            select_entity(*hit, result);
        }

        void select_entity(SelectedEntity selected, ViewerResult& result)
        {
            if (selected_entity_ != selected.id) {
                result.selected_entity_changed = true;
            }
            selected_entity_ = selected.id;
            selected_entity_frame_ = selected;
            navigation_anchor_.reset();
            mode_ = ViewMode::EntityDetail;
            detail_distance_ = 0.0F;
        }

        void select_adjacent_entity(RenderEntityView const& entities, int direction, ViewerResult& result)
        {
            if (!entities.valid()) {
                return;
            }
            auto const anchor = selected_entity_.has_value() ? selected_entity_ : navigation_anchor_;
            auto candidate = std::optional<SelectedEntity> {};
            auto wrapped = std::optional<SelectedEntity> {};
            for (std::size_t index = 0; index < entities.size(); ++index) {
                auto const position = entities.positions[index];
                auto const heading = entities.headings[index];
                if (!finite(position) || !finite(heading)) {
                    continue;
                }
                auto const current = SelectedEntity {
                    .id = entities.ids[index],
                    .position = position,
                    .heading = heading,
                    .hue = entities.hues[index],
                };
                if (!wrapped.has_value()
                    || (direction > 0 && current.id < wrapped->id)
                    || (direction < 0 && current.id > wrapped->id)) {
                    wrapped = current;
                }
                if (anchor.has_value()
                    && ((direction > 0 && current.id > *anchor) || (direction < 0 && current.id < *anchor))
                    && (!candidate.has_value()
                        || (direction > 0 && current.id < candidate->id)
                        || (direction < 0 && current.id > candidate->id))) {
                    candidate = current;
                }
            }
            if (candidate.has_value()) {
                select_entity(*candidate, result);
            } else if (wrapped.has_value()) {
                select_entity(*wrapped, result);
            }
        }

        void update_panel_input() noexcept
        {
            if (IsKeyPressed(KEY_F1)) {
                page_ = PanelPage::Overview;
            } else if (IsKeyPressed(KEY_F2)) {
                page_ = PanelPage::Network;
            } else if (IsKeyPressed(KEY_F3)) {
                page_ = PanelPage::Entity;
            }
            if (IsKeyPressed(KEY_F12)) {
                show_help_ = !show_help_;
            }
        }

        void update_controls(RenderFrame const& frame, ViewerResult& result)
        {
            auto const mouse = GetMousePosition();
            if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || mouse.x >= static_cast<float>(config_.panel_width)) {
                return;
            }
            auto constexpr button_height = 26.0F;
            if (page_ == PanelPage::Network) {
                return;
            }
            auto const button_count = page_ == PanelPage::Entity
                ? (selected_entity_.has_value() ? 2.0F : 0.0F)
                : 3.0F + (frame.stationary_observer.has_value() ? 3.0F : 0.0F)
                    + (frame.spatial.has_value() ? 1.0F : 0.0F)
                    + (frame.info.capabilities.can_pause_simulation ? 1.0F : 0.0F);
            if (button_count == 0.0F) {
                return;
            }
            auto const button_y = static_cast<float>(config_.window_height) - button_count * (button_height + 8.0F) - 18.0F;
            auto const button_at = [&](int index) {
                return Rectangle { 16.0F, button_y + static_cast<float>(index) * (button_height + 8.0F),
                    static_cast<float>(config_.panel_width) - 32.0F, button_height };
            };
            if (page_ == PanelPage::Entity) {
                if (selected_entity_.has_value() && CheckCollisionPointRec(mouse, button_at(0))) {
                    show_selected_debug_ = !show_selected_debug_;
                } else if (selected_entity_.has_value() && CheckCollisionPointRec(mouse, button_at(1))) {
                    clear_selection(result);
                }
                return;
            }
            if (CheckCollisionPointRec(mouse, button_at(0))) {
                show_bounds_ = !show_bounds_;
            } else if (CheckCollisionPointRec(mouse, button_at(1))) {
                show_axes_ = !show_axes_;
            } else if (CheckCollisionPointRec(mouse, button_at(2))) {
                if (mode_ == ViewMode::EntityDetail && selected_entity_frame_.has_value()) {
                    reset_detail_camera(max_distance_ * 0.25F);
                } else {
                    auto const center = Vec3f { target_.x, target_.y, target_.z };
                    reset_overview_camera(center);
                }
            } else if (frame.stationary_observer.has_value()
                && CheckCollisionPointRec(mouse, button_at(3))) {
                show_stationary_observer_ = !show_stationary_observer_;
            } else if (frame.stationary_observer.has_value()
                && CheckCollisionPointRec(mouse, button_at(4))) {
                show_stationary_observer_radius_ = !show_stationary_observer_radius_;
            } else if (frame.stationary_observer.has_value()
                && CheckCollisionPointRec(mouse, button_at(5))) {
                show_stationary_observer_frustum_ = !show_stationary_observer_frustum_;
            } else if (frame.spatial.has_value()
                && CheckCollisionPointRec(
                    mouse,
                    button_at(3 + (frame.stationary_observer.has_value() ? 3 : 0))
                )) {
                show_spatial_cells_ = !show_spatial_cells_;
            } else if (frame.info.capabilities.can_pause_simulation
                && CheckCollisionPointRec(mouse, button_at(
                    3 + (frame.stationary_observer.has_value() ? 3 : 0)
                        + (frame.spatial.has_value() ? 1 : 0)
                ))) {
                result.toggle_simulation_pause_requested = true;
            }
        }

        void clear_instances()
        {
            SIMNET_TRACE_SCOPE_CATEGORY("render.prepare.clear_buckets", simnet::LogCategory::Render);
            for (auto& bucket : transform_buckets_) {
                bucket.clear();
            }
        }

        void prepare_instances(RenderEntityView const& entities, RenderStats& stats)
        {
            clear_instances();
            {
                SIMNET_TRACE_SCOPE_CATEGORY("render.prepare.capacity_growth", simnet::LogCategory::Render);
                auto const per_bucket = entities.size() / hue_bucket_count + 1U;
                for (auto& bucket : transform_buckets_) {
                    if (bucket.capacity() < per_bucket) {
                        bucket.reserve(per_bucket);
                    }
                }
            }
            {
                SIMNET_TRACE_SCOPE_CATEGORY("render.prepare.transforms", simnet::LogCategory::Render);
                for (std::size_t index = 0; index < entities.size(); ++index) {
                    auto const position = entities.positions[index];
                    auto const heading = entities.headings[index];
                    if (!finite(position) || !finite(heading)) {
                        ++stats.skipped_entity_count;
                        continue;
                    }
                    transform_buckets_[hue_bucket(entities.hues[index])].push_back(
                        entity_transform(position, heading, config_.entity_scale)
                    );
                    ++stats.instance_count;
                }
            }
        }

        void draw_stationary_observer(
            StationaryObserverView const& observer,
            RenderStats& stats
        )
        {
            SIMNET_TRACE_SCOPE_CATEGORY(
                "render.stationary_observer_geometry",
                simnet::LogCategory::Render
            );
            auto const position = to_raylib(observer.position);
            auto const forward = normalized_or_forward(observer.forward);
            auto const direction_end = to_raylib(observer.position + forward * observer.interest_radius);
            DrawSphere(position, std::max(config_.entity_scale, 1.0F) * 1.5F, Color { 247, 184, 74, 255 });
            DrawLine3D(position, direction_end, Color { 247, 184, 74, 255 });
            if (show_stationary_observer_radius_) {
                DrawSphereWires(position, observer.interest_radius, 20, 20, Color { 247, 184, 74, 110 });
            }
            if (show_stationary_observer_frustum_) {
                auto const basis = world_up_basis(forward);
                auto const aspect = static_cast<float>(scene_rect_.width) / static_cast<float>(scene_rect_.height);
                auto const vertical = observer.vertical_fov_degrees * DEG2RAD;
                auto const vertical_half = std::tan(vertical * 0.5F) * observer.interest_radius;
                auto const horizontal_half = vertical_half * aspect;
                auto const center = observer.position + forward * observer.interest_radius;
                auto const corner = [&](float horizontal, float vertical_offset) {
                    return to_raylib(
                        center + basis.right * horizontal + basis.up * vertical_offset
                    );
                };
                auto const top_left = corner(-horizontal_half, vertical_half);
                auto const top_right = corner(horizontal_half, vertical_half);
                auto const bottom_left = corner(-horizontal_half, -vertical_half);
                auto const bottom_right = corner(horizontal_half, -vertical_half);
                auto const color = Color { 247, 184, 74, 150 };
                DrawLine3D(position, top_left, color);
                DrawLine3D(position, top_right, color);
                DrawLine3D(position, bottom_left, color);
                DrawLine3D(position, bottom_right, color);
                DrawLine3D(top_left, top_right, color);
                DrawLine3D(top_right, bottom_right, color);
                DrawLine3D(bottom_right, bottom_left, color);
                DrawLine3D(bottom_left, top_left, color);
            }
            ++stats.draw_calls;
        }

        void draw_spatial_cells(SpatialDebugView const& spatial, RenderStats& stats)
        {
            SIMNET_TRACE_SCOPE_CATEGORY("render.spatial_geometry", simnet::LogCategory::Render);
            for (auto const& cell : spatial.cells) {
                auto const intensity = static_cast<unsigned char>(std::min(220U, 55U + cell.entity_count * 12U));
                DrawBoundingBox(
                    {
                        .min = to_raylib(cell.bounds.min),
                        .max = to_raylib(cell.bounds.max),
                    },
                    Color { 85, 179, 226, intensity }
                );
            }
            if (!spatial.cells.empty()) {
                ++stats.draw_calls;
            }
        }

        void draw_debug_primitives(DebugPrimitiveView const& debug, RenderStats& stats)
        {
            SIMNET_TRACE_SCOPE_CATEGORY("render.selected_debug_geometry", simnet::LogCategory::Render);
            for (auto const& sphere : debug.spheres) {
                if (sphere.radius <= 0.0F || !std::isfinite(sphere.radius) || !finite(sphere.center)) {
                    continue;
                }
                DrawSphereWires(to_raylib(sphere.center), sphere.radius, 24, 12, to_raylib(sphere.color));
                ++stats.draw_calls;
            }
            for (auto const& vector : debug.vectors) {
                if (!finite(vector.origin) || !finite(vector.vector)
                    || simnet::length_squared(vector.vector) <= 0.000001F) {
                    continue;
                }
                DrawLine3D(
                    to_raylib(vector.origin),
                    to_raylib(vector.origin + vector.vector),
                    to_raylib(vector.color)
                );
                ++stats.draw_calls;
            }
            for (auto const& box : debug.boxes) {
                if (!finite(box.bounds.min) || !finite(box.bounds.max)) {
                    continue;
                }
                DrawBoundingBox(
                    { .min = to_raylib(box.bounds.min), .max = to_raylib(box.bounds.max) },
                    to_raylib(box.color)
                );
                ++stats.draw_calls;
            }
            for (auto const& cone : debug.cones) {
                if (!finite(cone.apex) || !finite(cone.direction)
                    || cone.length <= 0.0F || !std::isfinite(cone.length)
                    || cone.half_angle_degrees <= 0.0F
                    || cone.half_angle_degrees >= 180.0F
                    || !std::isfinite(cone.half_angle_degrees)) {
                    continue;
                }
                auto const forward = normalized_or_forward(cone.direction);
                auto right = cross(forward, { 0.0F, 1.0F, 0.0F });
                if (simnet::length_squared(right) <= 0.000001F) {
                    right = cross(forward, { 1.0F, 0.0F, 0.0F });
                }
                right = normalized_or_forward(right);
                auto const up = normalized_or_forward(cross(right, forward));
                auto const angle = cone.half_angle_degrees * pi / 180.0F;
                auto const forward_scale = std::cos(angle);
                auto const lateral_scale = std::sin(angle);
                auto previous = simnet::Vec3f {};
                auto constexpr segments = 12;
                for (auto index = 0; index <= segments; ++index) {
                    auto const around = 2.0F * pi * static_cast<float>(index)
                        / static_cast<float>(segments);
                    auto const lateral = right * std::cos(around) + up * std::sin(around);
                    auto const direction = normalized_or_forward(
                        forward * forward_scale + lateral * lateral_scale
                    );
                    auto const point = cone.apex + direction * cone.length;
                    DrawLine3D(to_raylib(cone.apex), to_raylib(point), to_raylib(cone.color));
                    if (index != 0) {
                        DrawLine3D(to_raylib(previous), to_raylib(point), to_raylib(cone.color));
                    }
                    previous = point;
                }
                ++stats.draw_calls;
            }
        }

        void draw_scene(RenderFrame const& frame, RenderStats& stats)
        {
            BeginTextureMode(scene_);
            ClearBackground(Color { 10, 13, 18, 255 });
            BeginMode3D(camera_);
            auto const aspect = static_cast<double>(scene_rect_.width) / static_cast<double>(scene_rect_.height);
            rlSetMatrixProjection(MatrixPerspective(camera_.fovy * DEG2RAD, aspect, 0.01, 10000.0));
            rlSetMatrixModelview(MatrixLookAt(camera_.position, camera_.target, camera_.up));

            if (show_bounds_) {
                auto const bounds = frame.info.world_bounds;
                auto const center = Vector3 {
                    (bounds.min.x + bounds.max.x) * 0.5F,
                    (bounds.min.y + bounds.max.y) * 0.5F,
                    (bounds.min.z + bounds.max.z) * 0.5F,
                };
                DrawCubeWires(center, bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y,
                    bounds.max.z - bounds.min.z, Color { 95, 112, 136, 180 });
            }
            if (show_axes_) {
                DrawLine3D({ 0.0F, 0.0F, 0.0F }, { 10.0F, 0.0F, 0.0F }, RED);
                DrawLine3D({ 0.0F, 0.0F, 0.0F }, { 0.0F, 10.0F, 0.0F }, GREEN);
                DrawLine3D({ 0.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 10.0F }, BLUE);
            }
            if (frame.stationary_observer.has_value()
                && mode_ != ViewMode::StationaryObserver
                && show_stationary_observer_) {
                draw_stationary_observer(*frame.stationary_observer, stats);
            }
            if (frame.spatial.has_value() && show_spatial_cells_) {
                draw_spatial_cells(*frame.spatial, stats);
            }
            if (show_selected_debug_ && !frame.debug_primitives.empty()) {
                draw_debug_primitives(frame.debug_primitives, stats);
            }

            if (instancing_available_) {
                for (std::size_t index = 0; index < transform_buckets_.size(); ++index) {
                    auto const& bucket = transform_buckets_[index];
                    if (bucket.empty()) {
                        continue;
                    }
                    auto const color = hue_color(static_cast<std::uint8_t>(index * 256U / hue_bucket_count));
                    for (int mesh_index = 0; mesh_index < model_.meshCount; ++mesh_index) {
                        auto const material_index = model_.meshMaterial[mesh_index];
                        if (material_index < 0 || material_index >= model_.materialCount) {
                            continue;
                        }
                        auto& material = model_.materials[material_index];
                        material.maps[MATERIAL_MAP_DIFFUSE].color = color;
                        DrawMeshInstanced(model_.meshes[mesh_index], material, bucket.data(), static_cast<int>(bucket.size()));
                        ++stats.draw_calls;
                    }
                    ++stats.active_hue_buckets;
                }
            }
            if (selected_entity_frame_.has_value()) {
                auto const radius = std::max(config_.picking_radius, config_.entity_scale * 1.5F);
                DrawSphereWires(to_raylib(selected_entity_frame_->position), radius, 8, 12, YELLOW);
                ++stats.draw_calls;
            }
            EndMode3D();
            EndTextureMode();
        }

        void draw_panel(RenderFrame const& frame, bool valid_entities, RenderStats const& stats, ViewerResult const& result)
        {
            auto const width = static_cast<float>(config_.panel_width);
            DrawRectangle(0, 0, static_cast<int>(config_.panel_width), static_cast<int>(config_.window_height),
                Color { 23, 28, 36, 255 });
            DrawLine(static_cast<int>(config_.panel_width) - 1, 0, static_cast<int>(config_.panel_width) - 1,
                static_cast<int>(config_.window_height), Color { 75, 88, 108, 255 });
            auto y = 18.0F;
            auto text = [&](char const* value, int size = 16, Color color = RAYWHITE) {
                DrawTextEx(font_, value, Vector2 { 16.0F, y }, static_cast<float>(size), 1.0F, color);
                y += static_cast<float>(size + 8);
            };
            auto section = [&](char const* value) {
                y += 8.0F;
                DrawLine(16, static_cast<int>(y), static_cast<int>(width) - 16, static_cast<int>(y), Color { 68, 82, 102, 255 });
                y += 10.0F;
                text(value, 17, Color { 133, 186, 235, 255 });
            };
            char line[192] {};
            text(config_.title.c_str(), 23);
            auto const mode_name = [](ViewMode mode) {
                switch (mode) {
                case ViewMode::Overview: return "Overview";
                case ViewMode::EntityDetail: return "Entity detail";
                case ViewMode::StationaryObserver: return "Stationary observer";
                case ViewMode::Game: return "Game";
                }
                return "Unknown";
            };
            auto const page_name = [](PanelPage page) {
                switch (page) {
                case PanelPage::Overview: return "F1 Overview";
                case PanelPage::Network: return "F2 Network";
                case PanelPage::Entity: return "F3 Entity";
                }
                return "Unknown";
            };
            text(page_name(page_), 17, Color { 133, 186, 235, 255 });
            auto button = [&](float& button_y, char const* label, bool active) {
                auto constexpr button_height = 26.0F;
                auto const rect = Rectangle { 16.0F, button_y, width - 32.0F, button_height };
                DrawRectangleRec(rect, active ? Color { 51, 102, 145, 255 } : Color { 45, 54, 68, 255 });
                DrawRectangleLinesEx(rect, 1.0F, Color { 91, 113, 140, 255 });
                DrawTextEx(font_, label, Vector2 { 24.0F, button_y + 5.0F }, 16.0F, 1.0F, RAYWHITE);
                button_y += button_height + 8.0F;
            };
            if (page_ == PanelPage::Overview) {
                section("Application");
                std::snprintf(line, sizeof(line), "mode %s", mode_name(mode_));
                text(line);
                if (frame.info.simulation_paused.has_value()) {
                    text(*frame.info.simulation_paused ? "simulation paused" : "simulation running");
                } else {
                    text("simulation state unavailable");
                }
                if (!frame.info.status_message.empty()) {
                    std::snprintf(
                        line,
                        sizeof(line),
                        "%.*s",
                        static_cast<int>(frame.info.status_message.size()),
                        frame.info.status_message.data()
                    );
                    text(line, 15, Color { 247, 184, 74, 255 });
                }
                section("World");
                std::snprintf(line, sizeof(line), "tick %llu", static_cast<unsigned long long>(frame.info.tick));
                text(line);
                std::snprintf(line, sizeof(line), "entities %zu", valid_entities ? frame.entities.size() : 0U);
                text(line);
                std::snprintf(line, sizeof(line), "skipped %u", stats.skipped_entity_count);
                text(line);
                if (frame.info.fixed_tick_rate_hz.has_value()) {
                    std::snprintf(line, sizeof(line), "fixed rate %.1f Hz", *frame.info.fixed_tick_rate_hz);
                    text(line);
                }
                if (!valid_entities) {
                    text("invalid entity view", 15, ORANGE);
                }
                if (frame.spatial.has_value()) {
                    auto const& spatial = *frame.spatial;
                    section("Spatial");
                    std::snprintf(line, sizeof(line), "occupied cells %u", spatial.occupied_cell_count);
                    text(line);
                    std::snprintf(line, sizeof(line), "displayed cells %zu", spatial.cells.size());
                    text(line);
                    text(spatial.display_capped ? "display capped yes" : "display capped no");
                    std::snprintf(line, sizeof(line), "max occupancy %u", spatial.max_cell_occupancy);
                    text(line);
                    std::snprintf(line, sizeof(line), "average occupancy %.2f", spatial.average_occupied_cell_load);
                    text(line);
                }
                section("Rendering");
                std::snprintf(line, sizeof(line), "FPS %d", GetFPS());
                text(line);
                std::snprintf(line, sizeof(line), "frame %.2f ms", static_cast<double>(frame.info.frame_delta.count()) / 1'000'000.0);
                text(line);
                if (frame.info.interpolation.has_value()) {
                    auto const& interpolation = *frame.info.interpolation;
                    std::snprintf(
                        line,
                        sizeof(line),
                        "interpolation %s alpha %.2f",
                        interpolation.enabled
                            ? (interpolation.interpolating ? "active" : "holding")
                            : "off",
                        interpolation.alpha
                    );
                    text(line);
                    std::snprintf(
                        line,
                        sizeof(line),
                        "display ticks %llu -> %llu",
                        static_cast<unsigned long long>(interpolation.from_tick),
                        static_cast<unsigned long long>(interpolation.to_tick)
                    );
                    text(line);
                }
                std::snprintf(line, sizeof(line), "input %.2f ms", static_cast<double>(stats.input_cpu_time.count()) / 1'000'000.0);
                text(line);
                std::snprintf(line, sizeof(line), "prepare %.2f ms", static_cast<double>(stats.preparation_cpu_time.count()) / 1'000'000.0);
                text(line);
                std::snprintf(line, sizeof(line), "scene %.2f ms", static_cast<double>(stats.scene_submit_cpu_time.count()) / 1'000'000.0);
                text(line);
                std::snprintf(line, sizeof(line), "panel %.2f ms", static_cast<double>(stats.panel_cpu_time.count()) / 1'000'000.0);
                text(line);
                std::snprintf(line, sizeof(line), "instances %u calls %u", stats.instance_count, stats.draw_calls);
                text(line);
                std::snprintf(line, sizeof(line), "hue buckets %u", stats.active_hue_buckets);
                text(line);
                section("Camera");
                std::snprintf(line, sizeof(line), "position %.1f %.1f %.1f", camera_.position.x, camera_.position.y, camera_.position.z);
                text(line);
                std::snprintf(line, sizeof(line), "target %.1f %.1f %.1f", camera_.target.x, camera_.target.y, camera_.target.z);
                text(line);
                auto const active_distance = mode_ == ViewMode::EntityDetail ? detail_distance_ : overview_distance_;
                std::snprintf(line, sizeof(line), "distance %.1f", active_distance);
                text(line);

                auto constexpr button_height = 26.0F;
                auto const button_count = 3.0F
                    + (frame.stationary_observer.has_value() ? 3.0F : 0.0F)
                    + (frame.spatial.has_value() ? 1.0F : 0.0F)
                    + (frame.info.capabilities.can_pause_simulation ? 1.0F : 0.0F);
                auto button_y = static_cast<float>(config_.window_height) - button_count * (button_height + 8.0F) - 18.0F;
                button(button_y, show_bounds_ ? "Hide bounds" : "Show bounds", show_bounds_);
                button(button_y, show_axes_ ? "Hide axes" : "Show axes", show_axes_);
                button(button_y, "Reset camera", false);
                if (frame.stationary_observer.has_value()) {
                    button(
                        button_y,
                        show_stationary_observer_
                            ? "Hide stationary observer"
                            : "Show stationary observer",
                        show_stationary_observer_
                    );
                    button(
                        button_y,
                        show_stationary_observer_radius_
                            ? "Hide observer radius"
                            : "Show observer radius",
                        show_stationary_observer_radius_
                    );
                    button(
                        button_y,
                        show_stationary_observer_frustum_
                            ? "Hide observer frustum"
                            : "Show observer frustum",
                        show_stationary_observer_frustum_
                    );
                }
                if (frame.spatial.has_value()) {
                    button(button_y, show_spatial_cells_ ? "Hide spatial cells" : "Show spatial cells", show_spatial_cells_);
                }
                if (frame.info.capabilities.can_pause_simulation) {
                    button(button_y, frame.info.simulation_paused.value_or(false) ? "Resume simulation" : "Pause simulation", false);
                }
            } else if (page_ == PanelPage::Network) {
                section("Connection");
                if (frame.info.connection.has_value()) {
                    std::snprintf(line, sizeof(line), "state %.*s", static_cast<int>(frame.info.connection->state.size()), frame.info.connection->state.data());
                    text(line);
                    if (frame.info.connection->peer.has_value()) {
                        std::snprintf(line, sizeof(line), "peer %u", *frame.info.connection->peer);
                        text(line);
                    }
                } else {
                    text("connection unavailable");
                }
                if (frame.info.session_ready.has_value()) {
                    text(*frame.info.session_ready ? "session ready" : "session not ready");
                }
                section("Replication");
                if (frame.info.replication.has_value()) {
                    auto const& replication = *frame.info.replication;
                    auto sequence = [&](char const* label, std::optional<SequenceId> value) {
                        if (value.has_value()) {
                            std::snprintf(line, sizeof(line), "%s %u", label, *value);
                            text(line);
                        }
                    };
                    sequence("emitted", replication.latest_emitted_sequence);
                    sequence("received", replication.latest_received_sequence);
                    sequence("applied", replication.latest_applied_sequence);
                    sequence("baseline", replication.acknowledged_baseline_sequence);
                    if (replication.latest_snapshot_tick.has_value()) {
                        std::snprintf(line, sizeof(line), "snapshot tick %llu", static_cast<unsigned long long>(*replication.latest_snapshot_tick));
                        text(line);
                    }
                    if (replication.retained_snapshot_count.has_value()) {
                        std::snprintf(line, sizeof(line), "retained %u", *replication.retained_snapshot_count);
                        text(line);
                    }
                    sequence("oldest", replication.oldest_retained_sequence);
                    sequence("newest", replication.newest_retained_sequence);
                } else {
                    text("replication unavailable");
                }
                section("Simulation");
                if (frame.info.simulation_paused.has_value()) {
                    text(*frame.info.simulation_paused ? "authoritative pause" : "authoritative running");
                } else {
                    text("simulation state unavailable");
                }
            } else {
                section("Selected Entity");
                if (!selected_entity_frame_.has_value()) {
                    text("Left click an entity to select it");
                    text("Use [ and ] to cycle visible IDs");
                } else {
                    auto const& selected = *selected_entity_frame_;
                    std::snprintf(line, sizeof(line), "id %u", selected.id);
                    text(line);
                    std::snprintf(line, sizeof(line), "position %.2f %.2f %.2f", selected.position.x, selected.position.y, selected.position.z);
                    text(line);
                    std::snprintf(line, sizeof(line), "heading %.2f %.2f %.2f", selected.heading.x, selected.heading.y, selected.heading.z);
                    text(line);
                    std::snprintf(line, sizeof(line), "hue %u", selected.hue);
                    text(line);
                    if (frame.selected_details.has_value() && frame.selected_details->id == selected.id) {
                        auto const& details = *frame.selected_details;
                        if (details.velocity.has_value()) {
                            std::snprintf(line, sizeof(line), "velocity %.2f %.2f %.2f", details.velocity->x, details.velocity->y, details.velocity->z);
                            text(line);
                        }
                        if (details.acceleration.has_value()) {
                            std::snprintf(line, sizeof(line), "acceleration %.2f %.2f %.2f", details.acceleration->x, details.acceleration->y, details.acceleration->z);
                            text(line);
                        }
                        if (details.speed.has_value()) {
                            std::snprintf(line, sizeof(line), "speed %.2f", *details.speed);
                            text(line);
                        }
                        if (details.raw_candidate_count.has_value()) {
                            std::snprintf(
                                line,
                                sizeof(line),
                                "neighbors raw %u kept %u/%u",
                                *details.raw_candidate_count,
                                details.retained_neighbor_count.value_or(0U),
                                details.maximum_neighbors.value_or(0U)
                            );
                            text(line);
                            std::snprintf(
                                line,
                                sizeof(line),
                                "accepted sep %u align %u coh %u",
                                details.separation_neighbor_count.value_or(0U),
                                details.alignment_neighbor_count.value_or(0U),
                                details.cohesion_neighbor_count.value_or(0U)
                            );
                            text(line);
                            std::snprintf(
                                line,
                                sizeof(line),
                                "hue neighbors %u",
                                details.hue_neighbor_count.value_or(0U)
                            );
                            text(line);
                        }
                        if (details.current_cell.has_value()) {
                            std::snprintf(
                                line,
                                sizeof(line),
                                "cell %d %d %d queried %u",
                                details.current_cell->x,
                                details.current_cell->y,
                                details.current_cell->z,
                                details.queried_cell_count.value_or(0U)
                            );
                            text(line);
                        }
                        if (details.query_radius.has_value()) {
                            std::snprintf(
                                line,
                                sizeof(line),
                                "radii query %.1f separation %.1f",
                                *details.query_radius,
                                details.separation_radius.value_or(0.0F)
                            );
                            text(line);
                            std::snprintf(
                                line,
                                sizeof(line),
                                "alignment %.1f cohesion %.1f",
                                details.alignment_radius.value_or(0.0F),
                                details.cohesion_radius.value_or(0.0F)
                            );
                            text(line);
                            std::snprintf(
                                line,
                                sizeof(line),
                                "FOV %.1f deg",
                                details.field_of_view_degrees.value_or(0.0F)
                            );
                            text(line);
                        }
                        if (details.neighbor_cap_hit.value_or(false)
                            || details.overlap_recovery.value_or(false)
                            || details.acceleration_saturated.value_or(false)
                            || details.wall_guard.value_or(false)) {
                            std::snprintf(
                                line,
                                sizeof(line),
                                "flags cap %s overlap %s accel-cap %s wall %s",
                                details.neighbor_cap_hit.value_or(false) ? "yes" : "no",
                                details.overlap_recovery.value_or(false) ? "yes" : "no",
                                details.acceleration_saturated.value_or(false) ? "yes" : "no",
                                details.wall_guard.value_or(false) ? "yes" : "no"
                            );
                            text(line);
                        }
                        auto vector = [&](char const* label, std::optional<Vec3f> const& value) {
                            if (!value.has_value()) {
                                return;
                            }
                            std::snprintf(
                                line,
                                sizeof(line),
                                "%s %.2f %.2f %.2f",
                                label,
                                value->x,
                                value->y,
                                value->z
                            );
                            text(line);
                        };
                        vector("separation", details.separation);
                        vector("alignment", details.alignment);
                        vector("cohesion", details.cohesion);
                        vector("containment", details.containment);
                        vector("wander", details.wander);
                        if (details.current_hue.has_value()) {
                            std::snprintf(
                                line,
                                sizeof(line),
                                "hue %.3f target %.3f",
                                *details.current_hue,
                                details.hue_target.value_or(*details.current_hue)
                            );
                            text(line);
                            std::snprintf(
                                line,
                                sizeof(line),
                                "hue delta %.4f step %.4f",
                                details.hue_delta.value_or(0.0F),
                                details.applied_hue_step.value_or(0.0F)
                            );
                            text(line);
                            std::snprintf(
                                line,
                                sizeof(line),
                                "active wander %s hue-assim %s hue-drift %s",
                                details.wander_active.value_or(false) ? "yes" : "no",
                                details.hue_assimilation_active.value_or(false) ? "yes" : "no",
                                details.hue_drift_active.value_or(false) ? "yes" : "no"
                            );
                            text(line);
                        }
                        if (details.last_update_tick.has_value()) {
                            std::snprintf(line, sizeof(line), "update tick %llu", static_cast<unsigned long long>(*details.last_update_tick));
                            text(line);
                        }
                        if (details.last_update_sequence.has_value()) {
                            std::snprintf(line, sizeof(line), "update sequence %u", *details.last_update_sequence);
                            text(line);
                        }
                        if (details.replicated.has_value()) {
                            text(*details.replicated ? "replicated" : "authoritative");
                        }
                    }
                }
                if (selected_entity_.has_value()) {
                    auto constexpr button_height = 26.0F;
                    auto button_y = static_cast<float>(config_.window_height)
                        - 2.0F * (button_height + 8.0F) - 18.0F;
                    button(
                        button_y,
                        show_selected_debug_ ? "Hide selected debug" : "Show selected debug",
                        show_selected_debug_
                    );
                    button(button_y, "Return to overview", false);
                }
            }
            static_cast<void>(result);
        }

        void draw_help_overlay(RenderFrame const& frame) const
        {
            auto const hint_position = Vector2 {
                static_cast<float>(scene_rect_.x + scene_rect_.width - 150),
                18.0F,
            };
            DrawTextEx(font_, "F12 Help", hint_position, 15.0F, 1.0F, Color { 180, 198, 220, 255 });
            if (!show_help_) {
                return;
            }
            auto const rect = Rectangle {
                static_cast<float>(scene_rect_.x + 36),
                48.0F,
                420.0F,
                frame.game_camera.has_value() ? 370.0F : 300.0F,
            };
            DrawRectangleRec(rect, Color { 20, 25, 33, 245 });
            DrawRectangleLinesEx(rect, 1.0F, Color { 91, 113, 140, 255 });
            auto y = rect.y + 18.0F;
            auto line = [&](char const* value, int size = 15, Color color = RAYWHITE) {
                DrawTextEx(font_, value, Vector2 { rect.x + 16.0F, y }, static_cast<float>(size), 1.0F, color);
                y += static_cast<float>(size + 7);
            };
            line("Viewer controls", 18, Color { 133, 186, 235, 255 });
            line("F1       Overview panel");
            line("F2       Network panel");
            line("F3       Entity panel");
            line("Left click entity  Select");
            line("[ / ]    Previous or next entity");
            line("Right drag  Orbit");
            line("Wheel     Zoom");
            line(frame.game_camera.has_value()
                ? "F4        Toggle Game / Overview"
                : frame.stationary_observer.has_value()
                    ? "F4        Toggle stationary observer"
                    : "F4        No special view");
            if (frame.game_camera.has_value()) {
                line("W / S     Pitch fish");
                line("A / D     Yaw fish");
                line("Shift     Accelerate");
                line("Ctrl      Slow down");
            }
            line("F5        Reset camera");
            if (frame.stationary_observer.has_value()) {
                line("Arrows    Rotate stationary observer");
            }
            line("Backspace Clear selection and overview");
            line("F12       Close help");
        }

        ViewerConfig config_;
        SceneRect scene_rect_ {};
        RenderTexture2D scene_ {};
        Font font_ {};
        Mesh mesh_ {};
        Model model_ {};
        Shader shader_ {};
        Camera3D camera_ {};
        Vector3 target_ {};
        ViewMode mode_ { ViewMode::Overview };
        bool instancing_available_ {};
        bool camera_initialized_ {};
        bool show_bounds_ { true };
        bool show_axes_ { true };
        bool show_stationary_observer_ { true };
        bool show_stationary_observer_radius_ { true };
        bool show_stationary_observer_frustum_ { true };
        bool show_spatial_cells_ {};
        bool show_selected_debug_ { true };
        bool show_help_ {};
        PanelPage page_ { PanelPage::Overview };
        float overview_yaw_ { pi * 0.25F };
        float overview_pitch_ { pi / 6.0F };
        float overview_distance_ { 10.0F };
        float detail_yaw_ { pi * 0.25F };
        float detail_pitch_ { pi / 6.0F };
        float detail_distance_ {};
        float min_distance_ { minimum_distance };
        float max_distance_ { 100.0F };
        float detail_min_distance_ { minimum_distance };
        float detail_max_distance_ { 100.0F };
        std::optional<EntityNetId> selected_entity_ {};
        std::optional<EntityNetId> navigation_anchor_ {};
        std::optional<SelectedEntity> selected_entity_frame_ {};
        std::array<std::vector<Matrix>, hue_bucket_count> transform_buckets_;
    };

    Viewer::Viewer(ViewerConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    Viewer::~Viewer() = default;
    Viewer::Viewer(Viewer&&) noexcept = default;
    Viewer& Viewer::operator=(Viewer&&) noexcept = default;

    ViewerResult Viewer::draw(RenderFrame const& frame)
    {
        if (!impl_) {
            throw std::runtime_error("cannot draw with a moved-from Viewer");
        }
        return impl_->draw(frame);
    }

    void Viewer::set_view_mode(ViewMode mode)
    {
        if (!impl_) {
            throw std::runtime_error("cannot configure a moved-from Viewer");
        }
        impl_->set_view_mode(mode);
    }
}

module;

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <simnet/telemetry_trace.hpp>

module simnet.render;

import :ui;
import :viewer_impl;
import simnet.core;
import simnet.telemetry;

namespace
{
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] std::filesystem::path viewer_font_path()
    {
        constexpr auto packaged_path = "assets/render/JetBrainsMonoNerdFont-Regular.ttf";
        if (std::filesystem::exists(packaged_path))
        {
            return packaged_path;
        }

        return "src/render/assets/JetBrainsMonoNerdFont-Regular.ttf";
    }

    constexpr float automatic_orbit_angular_speed = simnet::render_detail::pi / 18.0F;

    bool viewer_active = false;

    constexpr auto viewer_glyphs() noexcept
    {
        constexpr std::array icon_codepoints{
            0xf04b, // play
            0xf04c, // pause
            0xf030, // camera
            0xf03a, // overlays list
            0xf059, // help
            0xf201, // overview chart
            0xf1eb, // network
            0xf1b2, // entity cube
            0xf013, // setup gear
            0xf06e, // eye
            0xf11c, // keyboard
            0xf00c, // check
            0xf054, // chevron right
            0xf078, // chevron down
            0xf2f9, // reset
        };
        auto result = std::array<int, 95U + icon_codepoints.size()>{};
        for (auto index = std::size_t{}; index < 95U; ++index)
        {
            result[index] = static_cast<int>(32U + index);
        }
        std::copy(icon_codepoints.begin(), icon_codepoints.end(), result.begin() + 95);
        return result;
    }

    [[nodiscard]] simnet::Nanoseconds elapsed_ns(Clock::time_point start) noexcept
    {
        return std::chrono::duration_cast<simnet::Nanoseconds>(Clock::now() - start);
    }

    [[nodiscard]] float advance_orbit_yaw(float yaw, simnet::Nanoseconds frame_delta) noexcept
    {
        auto const elapsed = std::max(frame_delta, simnet::Nanoseconds{});
        auto const seconds = std::chrono::duration<float>{elapsed}.count();
        return std::remainder(
            yaw + automatic_orbit_angular_speed * seconds,
            2.0F * simnet::render_detail::pi
        );
    }

    [[nodiscard]] std::filesystem::path next_screenshot_path(std::string const& output_directory)
    {
        auto const directory = output_directory.empty()
                                   ? std::filesystem::current_path()
                                   : std::filesystem::absolute(output_directory).lexically_normal();
        std::filesystem::create_directories(directory);
        auto const stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch()
        )
                               .count();
        auto const stem = "screenshot_" + std::to_string(stamp);
        auto candidate = directory / (stem + ".png");
        for (auto suffix = std::uint32_t{1}; std::filesystem::exists(candidate); ++suffix)
        {
            candidate = directory / (stem + "_" + std::to_string(suffix) + ".png");
        }
        return candidate;
    }

    void validate_config(simnet::ViewerConfig const& config)
    {
        if (config.window_width == 0 || config.window_height == 0)
        {
            throw std::runtime_error("viewer window dimensions must be non-zero");
        }
        if (config.panel_width >= config.window_width)
        {
            throw std::runtime_error("viewer panel_width must be less than window_width");
        }
        if (config.target_frame_rate == 0)
        {
            throw std::runtime_error("viewer target_frame_rate must be non-zero");
        }
        if (config.entity_scale <= 0.0F || config.picking_radius <= 0.0F)
        {
            throw std::runtime_error("viewer entity_scale and picking_radius must be positive");
        }
        if (config.stationary_observer_interest_radius <= 0.0F ||
            config.stationary_observer_vertical_fov_degrees <= 0.0F ||
            config.stationary_observer_vertical_fov_degrees >= 180.0F ||
            config.max_visible_spatial_cells == 0U)
        {
            throw std::runtime_error("viewer stationary observer and spatial settings are invalid");
        }
    }

    [[nodiscard]] Mesh make_directional_mesh()
    {
        // The wedge points along local +Z and uses local +Y as up.
        auto mesh = Mesh{};
        mesh.vertexCount = 4;
        mesh.triangleCount = 4;
        mesh.vertices = static_cast<float*>(MemAlloc(sizeof(float) * 12));
        mesh.normals = static_cast<float*>(MemAlloc(sizeof(float) * 12));
        mesh.texcoords = static_cast<float*>(MemAlloc(sizeof(float) * 8));
        mesh.indices = static_cast<unsigned short*>(MemAlloc(sizeof(unsigned short) * 12));
        if (mesh.vertices == nullptr || mesh.normals == nullptr || mesh.texcoords == nullptr ||
            mesh.indices == nullptr)
        {
            MemFree(mesh.vertices);
            MemFree(mesh.normals);
            MemFree(mesh.texcoords);
            MemFree(mesh.indices);
            throw std::runtime_error("failed to allocate directional entity mesh");
        }

        constexpr std::array vertices = {
            0.0F,
            0.0F,
            1.25F,
            -0.45F,
            -0.25F,
            -0.65F,
            0.45F,
            -0.25F,
            -0.65F,
            0.0F,
            0.45F,
            -0.35F,
        };
        constexpr std::array normals = {
            0.0F,
            0.0F,
            1.0F,
            -0.5F,
            -0.3F,
            -0.8F,
            0.5F,
            -0.3F,
            -0.8F,
            0.0F,
            0.8F,
            -0.5F,
        };
        constexpr std::array texcoords = {0.5F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.5F, 0.5F};
        constexpr std::array<unsigned short, 12> indices = {0, 1, 2, 0, 2, 3, 0, 3, 1, 1, 3, 2};
        std::copy(vertices.begin(), vertices.end(), mesh.vertices);
        std::copy(normals.begin(), normals.end(), mesh.normals);
        std::copy(texcoords.begin(), texcoords.end(), mesh.texcoords);
        std::copy(indices.begin(), indices.end(), mesh.indices);
        UploadMesh(&mesh, false);
        return mesh;
    }

    [[nodiscard]] Matrix mesh_correction(std::string const& path)
    {
        if (std::filesystem::path{path}.filename() == "boid.obj")
        {
            // The reference boid already uses local +Z as forward and centimeter-sized
            // coordinates.
            return MatrixScale(0.05F, 0.05F, 0.05F);
        }
        return MatrixIdentity();
    }

    void bake_mesh_correction(Mesh& mesh, Matrix correction)
    {
        if (mesh.vertices == nullptr || mesh.vertexCount <= 0)
        {
            return;
        }
        for (auto index = 0; index < mesh.vertexCount; ++index)
        {
            auto const offset = index * 3;
            auto const corrected = Vector3Transform(
                {mesh.vertices[offset], mesh.vertices[offset + 1], mesh.vertices[offset + 2]},
                correction
            );
            mesh.vertices[offset] = corrected.x;
            mesh.vertices[offset + 1] = corrected.y;
            mesh.vertices[offset + 2] = corrected.z;
        }
        UpdateMeshBuffer(
            mesh,
            0,
            mesh.vertices,
            mesh.vertexCount * 3 * static_cast<int>(sizeof(float)),
            0
        );

        if (mesh.normals == nullptr)
        {
            return;
        }
        for (auto index = 0; index < mesh.vertexCount; ++index)
        {
            auto const offset = index * 3;
            auto const normal = Vector3{
                correction.m0 * mesh.normals[offset] + correction.m4 * mesh.normals[offset + 1] +
                    correction.m8 * mesh.normals[offset + 2],
                correction.m1 * mesh.normals[offset] + correction.m5 * mesh.normals[offset + 1] +
                    correction.m9 * mesh.normals[offset + 2],
                correction.m2 * mesh.normals[offset] + correction.m6 * mesh.normals[offset + 1] +
                    correction.m10 * mesh.normals[offset + 2],
            };
            auto const corrected = Vector3Normalize(normal);
            mesh.normals[offset] = corrected.x;
            mesh.normals[offset + 1] = corrected.y;
            mesh.normals[offset + 2] = corrected.z;
        }
        UpdateMeshBuffer(
            mesh,
            2,
            mesh.normals,
            mesh.vertexCount * 3 * static_cast<int>(sizeof(float)),
            0
        );
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
} // namespace

namespace simnet
{
    using render_detail::finite;
    using render_detail::normalized_or_forward;
    using render_detail::to_raylib;
    using render_detail::world_up_basis;

    Viewer::Impl::Impl(ViewerConfig config, std::string output_directory)
        : config_(std::move(config)), output_directory_(std::move(output_directory)),
          scene_rect_{
              .x = static_cast<int>(config_.panel_width),
              .y = 0,
              .width = static_cast<int>(config_.window_width - config_.panel_width),
              .height = static_cast<int>(config_.window_height),
          }
    {
        validate_config(config_);
        if (viewer_active)
        {
            throw std::runtime_error("only one Viewer may be active per process");
        }
        SetTraceLogLevel(LOG_WARNING);
        InitWindow(
            static_cast<int>(config_.window_width),
            static_cast<int>(config_.window_height),
            config_.title.c_str()
        );
        if (!IsWindowReady())
        {
            throw std::runtime_error("failed to create viewer window");
        }
        try
        {
            static constexpr auto glyphs = viewer_glyphs();
            auto const font_path = viewer_font_path().string();
            font_ = LoadFontEx(
                font_path.c_str(),
                40,
                glyphs.data(),
                static_cast<int>(glyphs.size())
            );
            if (font_.texture.id == 0)
            {
                throw std::runtime_error("failed to load viewer font");
            }
            SetTextureFilter(font_.texture, TEXTURE_FILTER_BILINEAR);
            SetTargetFPS(static_cast<int>(config_.target_frame_rate));
            scene_ = LoadRenderTexture(scene_rect_.width, scene_rect_.height);
            if (scene_.texture.id == 0)
            {
                throw std::runtime_error("failed to create viewer scene render texture");
            }
            load_entity_model();
            shader_ = LoadShaderFromMemory(instancing_vertex_shader, instancing_fragment_shader);
            instancing_available_ = shader_.id != 0 && model_.meshCount > 0 &&
                                    model_.materialCount > 0 && model_.meshes != nullptr &&
                                    model_.materials != nullptr && model_.meshMaterial != nullptr;
            if (instancing_available_)
            {
                shader_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader_, "mvp");
                shader_.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(shader_, "colDiffuse");
                for (int index = 0; index < model_.materialCount; ++index)
                {
                    model_.materials[index].shader = shader_;
                }
            }
            else
            {
                TraceLog(LOG_WARNING, "SimNet viewer instanced entity drawing is unavailable");
            }
            camera_.up = {0.0F, 1.0F, 0.0F};
            camera_.fovy = 55.0F;
            camera_.projection = CAMERA_PERSPECTIVE;
            viewer_active = true;
        }
        catch (...)
        {
            release_resources();
            throw;
        }
    }

    Viewer::Impl::~Impl()
    {
        release_resources();
    }

    void Viewer::Impl::release_resources() noexcept
    {
        if (model_.meshCount > 0)
        {
            UnloadModel(model_);
        }
        if (shader_.id != 0)
        {
            UnloadShader(shader_);
        }
        if (scene_.texture.id != 0)
        {
            UnloadRenderTexture(scene_);
        }
        if (font_.texture.id != 0)
        {
            UnloadFont(font_);
        }
        if (IsWindowReady())
        {
            CloseWindow();
        }
        viewer_active = false;
    }

    [[nodiscard]] ViewerResult Viewer::Impl::draw(RenderFrame const& frame)
    {
        SIMNET_TRACE_SCOPE_CATEGORY("render.viewer_frame", simnet::LogCategory::Render);
        auto const viewer_cpu_start = Clock::now();
        auto result = ViewerResult{};
        auto stats = RenderStats{};
        auto screenshot_requested = false;
        auto const input_start = Clock::now();
        {
            SIMNET_TRACE_SCOPE_CATEGORY("render.input", simnet::LogCategory::Render);
            update_panel_input(frame, result, screenshot_requested);
            update_camera(frame, result);
            update_selection(frame.entities, result);
            update_selected_trail();
        }
        stats.input_cpu_time = elapsed_ns(input_start);

        bool const valid_entities = frame.entities.valid();
        auto const preparation_start = Clock::now();
        {
            SIMNET_TRACE_SCOPE_CATEGORY("render.instance_preparation", simnet::LogCategory::Render);
            if (valid_entities)
            {
                prepare_instances(frame.entities, stats);
            }
            else
            {
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
        ClearBackground(render_detail::palette.window);
        auto const panel_start = Clock::now();
        {
            SIMNET_TRACE_SCOPE_CATEGORY("render.panel", simnet::LogCategory::Render);
            draw_panel(frame, valid_entities, stats, result);
        }
        stats.panel_cpu_time = elapsed_ns(panel_start);
        DrawTextureRec(
            scene_.texture,
            Rectangle{
                0.0F,
                0.0F,
                static_cast<float>(scene_rect_.width),
                -static_cast<float>(scene_rect_.height)
            },
            Vector2{static_cast<float>(scene_rect_.x), static_cast<float>(scene_rect_.y)},
            WHITE
        );
        draw_viewport_ui(frame, result);
        draw_help_overlay(frame);
        stats.viewer_cpu_time = elapsed_ns(viewer_cpu_start);
        if (screenshot_requested)
        {
            capture_screenshot();
        }
        {
            SIMNET_TRACE_SCOPE_CATEGORY("render.present_wait", simnet::LogCategory::Render);
            EndDrawing();
        }

        result.close_requested = WindowShouldClose();
        result.camera_mode = mode_;
        result.selected_entity = selected_entity_;
        result.stats = stats;
        completed_stats_ = stats;
        SIMNET_TRACE_PLOT("render.instances", static_cast<double>(stats.instance_count));
        SIMNET_TRACE_PLOT(
            "render.skipped_entities",
            static_cast<double>(stats.skipped_entity_count)
        );
        SIMNET_TRACE_PLOT("render.draw_calls", static_cast<double>(stats.draw_calls));
        SIMNET_TRACE_PLOT(
            "render.active_hue_buckets",
            static_cast<double>(stats.active_hue_buckets)
        );
        SIMNET_TRACE_PLOT(
            "render.selected_trail_points",
            static_cast<double>(selected_trail_.size())
        );
        if (frame.spatial.has_value())
        {
            SIMNET_TRACE_PLOT(
                "render.displayed_spatial_cells",
                static_cast<double>(frame.spatial->cells.size())
            );
        }
        return result;
    }

    void Viewer::Impl::set_camera_mode(CameraMode mode) noexcept
    {
        mode_ = mode == CameraMode::EntityFollow && !selected_entity_.has_value()
                    ? CameraMode::OverviewOrbit
                    : mode;
    }

    void Viewer::Impl::reset_active_camera(RenderFrame const& frame) noexcept
    {
        auto const bounds = frame.info.world_bounds;
        auto const center = Vec3f{
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
        if (mode_ == CameraMode::EntityFollow && selected_entity_frame_.has_value())
        {
            reset_detail_camera(extent);
        }
        else if (mode_ == CameraMode::OverviewOrbit)
        {
            reset_overview_camera(center);
        }
    }

    void Viewer::Impl::load_entity_model()
    {
        if (!config_.entity_mesh_path.empty())
        {
            model_ = LoadModel(config_.entity_mesh_path.c_str());
            if (model_.meshCount > 0 && model_.materialCount > 0 && model_.meshes != nullptr &&
                model_.materials != nullptr && model_.meshMaterial != nullptr)
            {
                auto const correction = mesh_correction(config_.entity_mesh_path);
                for (int index = 0; index < model_.meshCount; ++index)
                {
                    bake_mesh_correction(model_.meshes[index], correction);
                }
                return;
            }
            if (model_.meshCount > 0)
            {
                UnloadModel(model_);
                model_ = {};
            }
            TraceLog(LOG_WARNING, "SimNet viewer mesh load failed, using procedural wedge");
        }
        model_ = LoadModelFromMesh(make_directional_mesh());
    }

    [[nodiscard]] std::optional<Viewer::Impl::SelectedEntity>
    Viewer::Impl::find_selected_entity(RenderEntityView const& entities) const
    {
        if (!selected_entity_.has_value() || !entities.valid())
        {
            return std::nullopt;
        }
        for (std::size_t index = 0; index < entities.size(); ++index)
        {
            if (entities.ids[index] == *selected_entity_ && finite(entities.positions[index]) &&
                finite(entities.headings[index]))
            {
                return SelectedEntity{
                    .id = entities.ids[index],
                    .position = entities.positions[index],
                    .heading = entities.headings[index],
                    .hue = entities.hues[index],
                };
            }
        }
        return std::nullopt;
    }

    void Viewer::Impl::clear_selection(ViewerResult& result, bool preserve_navigation_anchor)
    {
        if (selected_entity_.has_value())
        {
            if (preserve_navigation_anchor)
            {
                navigation_anchor_ = selected_entity_;
            }
            else
            {
                navigation_anchor_.reset();
            }
            selected_entity_.reset();
            result.selected_entity_changed = true;
        }
        selected_entity_frame_.reset();
        selected_trail_.clear();
        mode_ = CameraMode::OverviewOrbit;
        ui_.page = render_detail::InspectorPage::Overview;
    }

    void Viewer::Impl::update_camera(RenderFrame const& frame, ViewerResult& result)
    {
        auto const bounds = frame.info.world_bounds;
        auto const center = Vec3f{
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
        min_distance_ = std::max(render_detail::minimum_distance, extent * 0.05F);
        max_distance_ = std::max(min_distance_ * 2.0F, extent * 4.0F);
        if (!camera_initialized_)
        {
            reset_overview_camera(center);
            camera_initialized_ = true;
        }

        if (mode_ == CameraMode::Game && !frame.game_camera.has_value())
        {
            mode_ = CameraMode::OverviewOrbit;
        }
        if (mode_ == CameraMode::StationaryObserver && !frame.stationary_observer.has_value())
        {
            mode_ = CameraMode::OverviewOrbit;
        }
        if (frame.stationary_observer.has_value())
        {
            result.stationary_observer_yaw_axis =
                (IsKeyDown(KEY_LEFT) ? 1.0F : 0.0F) - (IsKeyDown(KEY_RIGHT) ? 1.0F : 0.0F);
            result.stationary_observer_pitch_axis =
                (IsKeyDown(KEY_UP) ? 1.0F : 0.0F) - (IsKeyDown(KEY_DOWN) ? 1.0F : 0.0F);
        }

        selected_entity_frame_ = find_selected_entity(frame.entities);
        if (selected_entity_.has_value() && !selected_entity_frame_.has_value())
        {
            clear_selection(result, true);
        }

        auto const mouse = GetMousePosition();
        auto const in_scene = mouse.x >= static_cast<float>(scene_rect_.x) &&
                              mouse.x < static_cast<float>(scene_rect_.x + scene_rect_.width) &&
                              mouse.y >= 0.0F && mouse.y < static_cast<float>(scene_rect_.height);
        if ((mode_ == CameraMode::OverviewOrbit || mode_ == CameraMode::EntityFollow) && in_scene &&
            IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
        {
            auto const delta = GetMouseDelta();
            auto& yaw = mode_ == CameraMode::EntityFollow ? detail_yaw_ : overview_yaw_;
            auto& pitch = mode_ == CameraMode::EntityFollow ? detail_pitch_ : overview_pitch_;
            yaw -= delta.x * 0.006F;
            pitch = std::clamp(
                pitch + delta.y * 0.006F,
                render_detail::min_pitch,
                render_detail::max_pitch
            );
        }
        if ((mode_ == CameraMode::OverviewOrbit || mode_ == CameraMode::EntityFollow) && in_scene)
        {
            auto const wheel = GetMouseWheelMove();
            if (wheel != 0.0F)
            {
                auto& distance =
                    mode_ == CameraMode::EntityFollow ? detail_distance_ : overview_distance_;
                auto const minimum =
                    mode_ == CameraMode::EntityFollow ? detail_min_distance_ : min_distance_;
                auto const maximum =
                    mode_ == CameraMode::EntityFollow ? detail_max_distance_ : max_distance_;
                distance = std::clamp(distance * (1.0F - wheel * 0.1F), minimum, maximum);
            }
        }
        if (automatic_orbit_enabled_)
        {
            if (mode_ == CameraMode::OverviewOrbit)
            {
                overview_yaw_ = advance_orbit_yaw(overview_yaw_, frame.info.frame_delta);
            }
            else if (mode_ == CameraMode::EntityFollow)
            {
                detail_yaw_ = advance_orbit_yaw(detail_yaw_, frame.info.frame_delta);
            }
        }
        if (mode_ == CameraMode::Game && frame.game_camera.has_value())
        {
            camera_.position = to_raylib(frame.game_camera->position);
            camera_.target = to_raylib(frame.game_camera->target);
            camera_.up = to_raylib(frame.game_camera->up);
            camera_.fovy = frame.game_camera->vertical_fov_degrees;
            result.player_input = {
                .pitch_up = IsKeyDown(KEY_W),
                .yaw_left = IsKeyDown(KEY_A),
                .pitch_down = IsKeyDown(KEY_S),
                .yaw_right = IsKeyDown(KEY_D),
                .accelerate = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT),
                .decelerate = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL),
                .left_mouse = IsMouseButtonDown(MOUSE_BUTTON_LEFT),
                .right_mouse = IsMouseButtonDown(MOUSE_BUTTON_RIGHT),
            };
        }
        else if (mode_ == CameraMode::StationaryObserver && frame.stationary_observer.has_value())
        {
            auto const forward = normalized_or_forward(frame.stationary_observer->forward);
            auto const basis = world_up_basis(forward);
            camera_.position = to_raylib(frame.stationary_observer->position);
            camera_.target = to_raylib(frame.stationary_observer->position + forward);
            camera_.up = to_raylib(basis.up);
            camera_.fovy = frame.stationary_observer->vertical_fov_degrees;
        }
        else if (mode_ == CameraMode::EntityFollow && selected_entity_frame_.has_value())
        {
            target_ = to_raylib(selected_entity_frame_->position);
            detail_min_distance_ = std::max(0.5F, config_.entity_scale * 1.5F);
            detail_max_distance_ = std::max(detail_min_distance_ * 4.0F, extent * 0.5F);
            if (detail_distance_ <= 0.0F)
            {
                reset_detail_camera(extent);
            }
            update_camera_position(detail_yaw_, detail_pitch_, detail_distance_);
        }
        else
        {
            target_ = to_raylib(center);
            update_camera_position(overview_yaw_, overview_pitch_, overview_distance_);
            camera_.fovy = 55.0F;
        }
    }

    void Viewer::Impl::update_camera_position(float yaw, float pitch, float distance) noexcept
    {
        auto const cosine_pitch = std::cos(pitch);
        camera_.target = target_;
        camera_.position = {
            target_.x + distance * cosine_pitch * std::sin(yaw),
            target_.y + distance * std::sin(pitch),
            target_.z + distance * cosine_pitch * std::cos(yaw),
        };
    }

    void Viewer::Impl::reset_overview_camera(Vec3f center) noexcept
    {
        target_ = to_raylib(center);
        overview_yaw_ = render_detail::pi * 0.25F;
        overview_pitch_ = render_detail::pi / 6.0F;
        overview_distance_ = std::clamp(max_distance_ * 0.45F, min_distance_, max_distance_);
    }

    void Viewer::Impl::reset_detail_camera(float world_extent) noexcept
    {
        detail_yaw_ = render_detail::pi * 0.25F;
        detail_pitch_ = render_detail::pi / 6.0F;
        detail_distance_ = std::clamp(
            std::max(config_.entity_scale * 10.0F, world_extent * 0.03F),
            detail_min_distance_,
            detail_max_distance_
        );
    }

    [[nodiscard]] bool Viewer::Impl::mouse_in_scene(Vector2 mouse) const noexcept
    {
        return mouse.x >= static_cast<float>(scene_rect_.x) &&
               mouse.x < static_cast<float>(scene_rect_.x + scene_rect_.width) &&
               mouse.y >= static_cast<float>(scene_rect_.y) &&
               mouse.y < static_cast<float>(scene_rect_.y + scene_rect_.height);
    }

    [[nodiscard]] std::optional<float>
    Viewer::Impl::ray_sphere_hit_distance(Ray ray, Vector3 center, float radius) noexcept
    {
        auto const offset = Vector3Subtract(ray.position, center);
        auto const projection = Vector3DotProduct(offset, ray.direction);
        auto const discriminant =
            projection * projection - (Vector3DotProduct(offset, offset) - radius * radius);
        if (discriminant < 0.0F)
        {
            return std::nullopt;
        }
        auto const root = std::sqrt(discriminant);
        auto const near_distance = -projection - root;
        auto const far_distance = -projection + root;
        if (near_distance >= 0.0F)
        {
            return near_distance;
        }
        if (far_distance >= 0.0F)
        {
            return far_distance;
        }
        return std::nullopt;
    }

    void Viewer::Impl::update_selection(RenderEntityView const& entities, ViewerResult& result)
    {
        if (mode_ == CameraMode::Game)
        {
            return;
        }
        if (ui_.pointer_captured)
        {
            return;
        }
        if (IsKeyPressed(KEY_LEFT_BRACKET))
        {
            select_adjacent_entity(entities, -1, result);
            return;
        }
        if (IsKeyPressed(KEY_RIGHT_BRACKET))
        {
            select_adjacent_entity(entities, 1, result);
            return;
        }
        auto const mouse = GetMousePosition();
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || !mouse_in_scene(mouse) || !entities.valid())
        {
            return;
        }
        auto const scene_mouse = Vector2{
            mouse.x - static_cast<float>(scene_rect_.x),
            mouse.y - static_cast<float>(scene_rect_.y),
        };
        auto const ray =
            GetScreenToWorldRayEx(scene_mouse, camera_, scene_rect_.width, scene_rect_.height);
        auto nearest_distance = std::numeric_limits<float>::infinity();
        auto hit = std::optional<SelectedEntity>{};
        for (std::size_t index = 0; index < entities.size(); ++index)
        {
            auto const position = entities.positions[index];
            auto const heading = entities.headings[index];
            if (!finite(position) || !finite(heading))
            {
                continue;
            }
            auto const distance =
                ray_sphere_hit_distance(ray, to_raylib(position), config_.picking_radius);
            if (distance.has_value() && *distance < nearest_distance)
            {
                nearest_distance = *distance;
                hit = SelectedEntity{
                    .id = entities.ids[index],
                    .position = position,
                    .heading = heading,
                    .hue = entities.hues[index],
                };
            }
        }
        if (!hit.has_value())
        {
            return;
        }
        select_entity(*hit, result);
    }

    void Viewer::Impl::select_entity(SelectedEntity selected, ViewerResult& result)
    {
        if (selected_entity_ != selected.id)
        {
            result.selected_entity_changed = true;
            selected_trail_.clear();
        }
        selected_entity_ = selected.id;
        selected_entity_frame_ = selected;
        navigation_anchor_.reset();
        mode_ = CameraMode::EntityFollow;
        ui_.page = render_detail::InspectorPage::Entity;
        ui_.page_scroll[static_cast<std::size_t>(render_detail::InspectorPage::Entity)] = 0.0F;
        detail_distance_ = 0.0F;
    }

    void Viewer::Impl::update_selected_trail()
    {
        if (!selected_entity_frame_.has_value())
        {
            return;
        }
        auto const position = selected_entity_frame_->position;
        auto const minimum_distance = std::max(0.05F, config_.entity_scale * 0.15F);
        if (!selected_trail_.empty() &&
            length_squared(position - selected_trail_.back()) < minimum_distance * minimum_distance)
        {
            return;
        }
        selected_trail_.push_back(position);
        if (selected_trail_.size() > render_detail::selected_trail_max_points)
        {
            selected_trail_.pop_front();
        }
    }

    void Viewer::Impl::select_adjacent_entity(
        RenderEntityView const& entities,
        int direction,
        ViewerResult& result
    )
    {
        if (!entities.valid())
        {
            return;
        }
        auto const anchor = selected_entity_.has_value() ? selected_entity_ : navigation_anchor_;
        auto candidate = std::optional<SelectedEntity>{};
        auto wrapped = std::optional<SelectedEntity>{};
        for (std::size_t index = 0; index < entities.size(); ++index)
        {
            auto const position = entities.positions[index];
            auto const heading = entities.headings[index];
            if (!finite(position) || !finite(heading))
            {
                continue;
            }
            auto const current = SelectedEntity{
                .id = entities.ids[index],
                .position = position,
                .heading = heading,
                .hue = entities.hues[index],
            };
            if (!wrapped.has_value() || (direction > 0 && current.id < wrapped->id) ||
                (direction < 0 && current.id > wrapped->id))
            {
                wrapped = current;
            }
            if (anchor.has_value() &&
                ((direction > 0 && current.id > *anchor) ||
                 (direction < 0 && current.id < *anchor)) &&
                (!candidate.has_value() || (direction > 0 && current.id < candidate->id) ||
                 (direction < 0 && current.id > candidate->id)))
            {
                candidate = current;
            }
        }
        if (candidate.has_value())
        {
            select_entity(*candidate, result);
        }
        else if (wrapped.has_value())
        {
            select_entity(*wrapped, result);
        }
    }

    void Viewer::Impl::capture_screenshot() const
    {
        try
        {
            auto const path = next_screenshot_path(output_directory_);
            auto const raylib_path = path.lexically_relative(std::filesystem::current_path());
            if (raylib_path.empty())
            {
                throw std::runtime_error(
                    "screenshot path cannot be expressed relative to the working directory"
                );
            }
            // The back buffer holds the complete current frame before EndDrawing swaps it.
            rlDrawRenderBatchActive();
            TakeScreenshot(raylib_path.string().c_str());
            auto error = std::error_code{};
            if (!std::filesystem::is_regular_file(path, error) || error)
            {
                log(LogCategory::Render,
                    LogLevel::Error,
                    "viewer screenshot failed path=" + path.string());
                return;
            }
            log(LogCategory::Render,
                LogLevel::Info,
                "viewer screenshot saved path=" + path.string());
        }
        catch (std::exception const& error)
        {
            log(LogCategory::Render,
                LogLevel::Error,
                "viewer screenshot failed: " + std::string{error.what()});
        }
    }

    Viewer::Viewer(ViewerConfig config, std::string output_directory)
        : impl_(std::make_unique<Impl>(std::move(config), std::move(output_directory)))
    {
    }

    Viewer::~Viewer() = default;
    Viewer::Viewer(Viewer&&) noexcept = default;
    Viewer& Viewer::operator=(Viewer&&) noexcept = default;

    ViewerResult Viewer::draw(RenderFrame const& frame)
    {
        if (!impl_)
        {
            throw std::runtime_error("cannot draw with a moved-from Viewer");
        }
        return impl_->draw(frame);
    }

    void Viewer::set_camera_mode(CameraMode mode)
    {
        if (!impl_)
        {
            throw std::runtime_error("cannot configure a moved-from Viewer");
        }
        impl_->set_camera_mode(mode);
    }
} // namespace simnet

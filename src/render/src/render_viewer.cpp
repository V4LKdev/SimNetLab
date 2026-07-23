module;

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include "../assets/jetbrains_mono_regular.hpp"

module simnet.render;

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

    struct FallbackInstance
    {
        Vector3 position {};
        Color color {};
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
    }

    [[nodiscard]] Vector3 to_raylib(simnet::Vec3f value) noexcept
    {
        return { value.x, value.y, value.z };
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

    [[nodiscard]] Matrix entity_transform(simnet::Vec3f position, simnet::Vec3f heading, float scale) noexcept
    {
        auto const forward = normalized_or_forward(heading);
        auto reference_up = simnet::Vec3f { 0.0F, 1.0F, 0.0F };
        if (std::abs(simnet::dot(forward, reference_up)) > 0.98F) {
            reference_up = { 1.0F, 0.0F, 0.0F };
        }
        auto const right = normalized_or_forward(cross(reference_up, forward));
        auto const up = cross(forward, right);

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
            mesh_ = make_directional_mesh();
            model_ = LoadModelFromMesh(mesh_);
            shader_ = LoadShaderFromMemory(instancing_vertex_shader, instancing_fragment_shader);
            instancing_available_ = shader_.id != 0 && model_.meshCount > 0 && model_.materialCount > 0;
            if (instancing_available_) {
                shader_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader_, "mvp");
                shader_.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(shader_, "colDiffuse");
                model_.materials[0].shader = shader_;
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
            auto result = ViewerResult {};
            result.view_mode = mode_;
            auto stats = RenderStats {};
            auto const input_start = Clock::now();
            update_camera(frame.info, result);
            update_controls(frame.info, result);
            stats.input_cpu_time = elapsed_ns(input_start);

            bool const valid_entities = frame.entities.valid();
            auto const preparation_start = Clock::now();
            if (valid_entities) {
                prepare_instances(frame.entities, stats);
            } else {
                clear_instances();
            }
            stats.preparation_cpu_time = elapsed_ns(preparation_start);

            auto const scene_start = Clock::now();
            draw_scene(frame.info, stats);
            stats.scene_submit_cpu_time = elapsed_ns(scene_start);

            BeginDrawing();
            ClearBackground(Color { 18, 21, 27, 255 });
            auto const panel_start = Clock::now();
            draw_panel(frame, valid_entities, stats, result);
            stats.panel_cpu_time = elapsed_ns(panel_start);
            DrawTextureRec(
                scene_.texture,
                Rectangle { 0.0F, 0.0F, static_cast<float>(scene_rect_.width), -static_cast<float>(scene_rect_.height) },
                Vector2 { static_cast<float>(scene_rect_.x), static_cast<float>(scene_rect_.y) },
                WHITE
            );
            EndDrawing();

            result.close_requested = WindowShouldClose();
            result.stats = stats;
            return result;
        }

        void set_view_mode(ViewMode mode) noexcept
        {
            mode_ = mode;
        }

    private:
        void update_camera(RenderFrameInfo const& info, ViewerResult& result)
        {
            auto const bounds = info.world_bounds;
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
            target_ = to_raylib(center);
            min_distance_ = std::max(minimum_distance, extent * 0.05F);
            max_distance_ = std::max(min_distance_ * 2.0F, extent * 4.0F);
            if (!camera_initialized_) {
                reset_camera();
                camera_initialized_ = true;
            }

            auto const mouse = GetMousePosition();
            auto const in_scene = mouse.x >= static_cast<float>(scene_rect_.x)
                && mouse.x < static_cast<float>(scene_rect_.x + scene_rect_.width)
                && mouse.y >= 0.0F
                && mouse.y < static_cast<float>(scene_rect_.height);
            if (mode_ == ViewMode::Overview && in_scene && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                auto const delta = GetMouseDelta();
                yaw_ -= delta.x * 0.006F;
                pitch_ = std::clamp(pitch_ + delta.y * 0.006F, min_pitch, max_pitch);
            }
            if (mode_ == ViewMode::Overview && in_scene) {
                auto const wheel = GetMouseWheelMove();
                if (wheel != 0.0F) {
                    distance_ = std::clamp(distance_ * (1.0F - wheel * 0.1F), min_distance_, max_distance_);
                }
            }
            if (IsKeyPressed(KEY_R)) {
                reset_camera();
            }
            if (mode_ != ViewMode::Overview) {
                mode_ = ViewMode::Overview;
                result.view_mode = mode_;
            }
            auto const cosine_pitch = std::cos(pitch_);
            camera_.target = target_;
            camera_.position = {
                target_.x + distance_ * cosine_pitch * std::sin(yaw_),
                target_.y + distance_ * std::sin(pitch_),
                target_.z + distance_ * cosine_pitch * std::cos(yaw_),
            };
        }

        void reset_camera()
        {
            yaw_ = pi * 0.25F;
            pitch_ = pi / 6.0F;
            distance_ = std::clamp(max_distance_ * 0.45F, min_distance_, max_distance_);
        }

        void update_controls(RenderFrameInfo const& info, ViewerResult& result)
        {
            auto const mouse = GetMousePosition();
            if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || mouse.x >= static_cast<float>(config_.panel_width)) {
                return;
            }
            auto constexpr button_height = 26.0F;
            auto const button_y = static_cast<float>(config_.window_height) - 5.0F * (button_height + 8.0F) - 18.0F;
            auto const button_at = [&](int index) {
                return Rectangle { 16.0F, button_y + static_cast<float>(index) * (button_height + 8.0F),
                    static_cast<float>(config_.panel_width) - 32.0F, button_height };
            };
            if (CheckCollisionPointRec(mouse, button_at(0))) {
                show_bounds_ = !show_bounds_;
            } else if (CheckCollisionPointRec(mouse, button_at(1))) {
                show_axes_ = !show_axes_;
            } else if (CheckCollisionPointRec(mouse, button_at(2))) {
                reset_camera();
            } else if (info.capabilities.can_pause_simulation && CheckCollisionPointRec(mouse, button_at(3))) {
                result.toggle_simulation_pause_requested = true;
            }
        }

        void clear_instances()
        {
            for (auto& bucket : transform_buckets_) {
                bucket.clear();
            }
            fallback_instances_.clear();
        }

        void prepare_instances(RenderEntityView const& entities, RenderStats& stats)
        {
            clear_instances();
            auto const per_bucket = entities.size() / hue_bucket_count + 1U;
            for (auto& bucket : transform_buckets_) {
                if (bucket.capacity() < per_bucket) {
                    bucket.reserve(per_bucket);
                }
            }
            if (!instancing_available_ && fallback_instances_.capacity() < entities.size()) {
                fallback_instances_.reserve(entities.size());
            }

            for (std::size_t index = 0; index < entities.size(); ++index) {
                auto const position = entities.positions[index];
                auto const heading = entities.headings[index];
                if (!finite(position) || !finite(heading)) {
                    ++stats.skipped_entity_count;
                    continue;
                }
                auto const color = hue_color(entities.hues[index]);
                transform_buckets_[hue_bucket(entities.hues[index])].push_back(
                    entity_transform(position, heading, config_.entity_scale)
                );
                if (!instancing_available_) {
                    fallback_instances_.push_back({ .position = to_raylib(position), .color = color });
                }
                ++stats.instance_count;
            }
        }

        void draw_scene(RenderFrameInfo const& info, RenderStats& stats)
        {
            BeginTextureMode(scene_);
            ClearBackground(Color { 10, 13, 18, 255 });
            BeginMode3D(camera_);
            auto const aspect = static_cast<double>(scene_rect_.width) / static_cast<double>(scene_rect_.height);
            rlSetMatrixProjection(MatrixPerspective(camera_.fovy * DEG2RAD, aspect, 0.01, 10000.0));
            rlSetMatrixModelview(MatrixLookAt(camera_.position, camera_.target, camera_.up));

            if (show_bounds_) {
                auto const bounds = info.world_bounds;
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

            if (instancing_available_) {
                for (std::size_t index = 0; index < transform_buckets_.size(); ++index) {
                    auto const& bucket = transform_buckets_[index];
                    if (bucket.empty()) {
                        continue;
                    }
                    model_.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = hue_color(
                        static_cast<std::uint8_t>(index * 256U / hue_bucket_count)
                    );
                    DrawMeshInstanced(model_.meshes[0], model_.materials[0], bucket.data(), static_cast<int>(bucket.size()));
                    ++stats.draw_calls;
                    ++stats.active_hue_buckets;
                }
            } else {
                for (auto const& instance : fallback_instances_) {
                    DrawCube(instance.position, config_.entity_scale, config_.entity_scale, config_.entity_scale, instance.color);
                }
                stats.draw_calls = fallback_instances_.empty() ? 0U : static_cast<std::uint32_t>(fallback_instances_.size());
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
            auto text = [&](char const* value, int size = 15, Color color = RAYWHITE) {
                DrawTextEx(font_, value, Vector2 { 16.0F, y }, static_cast<float>(size), 1.0F, color);
                y += static_cast<float>(size + 7);
            };
            auto section = [&](char const* value) {
                y += 8.0F;
                DrawLine(16, static_cast<int>(y), static_cast<int>(width) - 16, static_cast<int>(y), Color { 68, 82, 102, 255 });
                y += 10.0F;
                text(value, 16, Color { 133, 186, 235, 255 });
            };
            char line[192] {};
            text(config_.title.c_str(), 24);
            section("Application");
            std::snprintf(line, sizeof(line), "mode Overview");
            text(line);
            if (!valid_entities) {
                text("invalid entity view", 15, ORANGE);
            }
            section("Connection");
            if (frame.info.connection.has_value()) {
                std::snprintf(
                    line,
                    sizeof(line),
                    "state %.*s",
                    static_cast<int>(frame.info.connection->state.size()),
                    frame.info.connection->state.data()
                );
                text(line);
                if (frame.info.connection->peer.has_value()) {
                    std::snprintf(line, sizeof(line), "peer %u", *frame.info.connection->peer);
                    text(line);
                }
            } else {
                text("state unavailable");
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
                    std::snprintf(
                        line,
                        sizeof(line),
                        "snapshot tick %llu",
                        static_cast<unsigned long long>(*replication.latest_snapshot_tick)
                    );
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
                text(*frame.info.simulation_paused ? "simulation paused" : "simulation running");
            } else {
                text("simulation state unavailable");
            }
            std::snprintf(line, sizeof(line), "entities %zu", valid_entities ? frame.entities.size() : 0U);
            text(line);
            std::snprintf(line, sizeof(line), "skipped %u", stats.skipped_entity_count);
            text(line);
            std::snprintf(line, sizeof(line), "tick %llu", static_cast<unsigned long long>(frame.info.tick));
            text(line);
            if (frame.info.snapshot_sequence.has_value()) {
                std::snprintf(line, sizeof(line), "sequence %u", *frame.info.snapshot_sequence);
                text(line);
            }
            if (frame.info.fixed_tick_rate_hz.has_value()) {
                std::snprintf(line, sizeof(line), "fixed rate %.1f Hz", *frame.info.fixed_tick_rate_hz);
                text(line);
            }
            section("Rendering");
            std::snprintf(line, sizeof(line), "FPS %d", GetFPS());
            text(line);
            std::snprintf(line, sizeof(line), "frame %.2f ms", static_cast<double>(frame.info.frame_delta.count()) / 1'000'000.0);
            text(line);
            std::snprintf(line, sizeof(line), "prepare %.2f ms", static_cast<double>(stats.preparation_cpu_time.count()) / 1'000'000.0);
            text(line);
            std::snprintf(line, sizeof(line), "scene %.2f ms", static_cast<double>(stats.scene_submit_cpu_time.count()) / 1'000'000.0);
            text(line);
            std::snprintf(line, sizeof(line), "instances %u calls %u", stats.instance_count, stats.draw_calls);
            text(line);
            std::snprintf(line, sizeof(line), "hue buckets %u", stats.active_hue_buckets);
            text(line);
            section("Camera");
            std::snprintf(line, sizeof(line), "pos %.1f %.1f %.1f", camera_.position.x, camera_.position.y, camera_.position.z);
            text(line);
            std::snprintf(line, sizeof(line), "target %.1f %.1f %.1f", camera_.target.x, camera_.target.y, camera_.target.z);
            text(line);
            std::snprintf(line, sizeof(line), "distance %.1f", distance_);
            text(line);
            text("Right drag orbit");
            text("Wheel zoom");
            text("R reset");

            auto constexpr button_height = 26.0F;
            auto button_y = static_cast<float>(config_.window_height) - 5.0F * (button_height + 8.0F) - 18.0F;
            auto button = [&](char const* label, bool active) {
                auto const rect = Rectangle { 16.0F, button_y, width - 32.0F, button_height };
                DrawRectangleRec(rect, active ? Color { 51, 102, 145, 255 } : Color { 45, 54, 68, 255 });
                DrawRectangleLinesEx(rect, 1.0F, Color { 91, 113, 140, 255 });
                DrawTextEx(font_, label, Vector2 { 24.0F, button_y + 5.0F }, 16.0F, 1.0F, RAYWHITE);
                button_y += button_height + 8.0F;
            };
            button(show_bounds_ ? "Hide bounds" : "Show bounds", show_bounds_);
            button(show_axes_ ? "Hide axes" : "Show axes", show_axes_);
            button("Reset camera", false);
            if (frame.info.capabilities.can_pause_simulation) {
                button(*frame.info.simulation_paused ? "Resume simulation" : "Pause simulation", false);
            }
            static_cast<void>(result);
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
        float yaw_ { pi * 0.25F };
        float pitch_ { pi / 6.0F };
        float distance_ { 10.0F };
        float min_distance_ { minimum_distance };
        float max_distance_ { 100.0F };
        std::array<std::vector<Matrix>, hue_bucket_count> transform_buckets_;
        std::vector<FallbackInstance> fallback_instances_;
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

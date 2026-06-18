#include "renderer.hpp"

#include <cmath>
#include <raylib.h>
#include <raymath.h>

#include "../game/shared/components.hpp"
#include "telemetry.hpp"
#include "fonts/font_jetbrains.h"

namespace simnet::client {
    Renderer::Renderer(const int width, const int height, const std::string_view title, const flecs::world &world)
        : world_(world),
          width_(width),
          height_(height),
          boid_query_(
              world.query_builder<const ecs::Position, const ecs::Heading, const ecs::Hue>().with<ecs::Boid>().cached().
              build()),
          camera_distance_(0),
          camera_yaw_(0),
          camera_pitch_(0),
          camera_(init_camera(world.get<SimConfig>()))
    {
        int flags = FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE;

        // SetConfigFlags(flags);
        SetTargetFPS(120);
        SetExitKey(0);
        SetTraceLogLevel(LOG_WARNING);

        InitWindow(width_, height_, title.data());
        // Fatal issue with raylib
        if (!IsWindowReady()) {
            TELEM_LOG_ERROR("Fatal: unable to initialise OpenGL window");
            std::abort();
        }

        TELEM_LOG_INFO("{}", GetWorkingDirectory());

        boid_model_ = LoadModel("assets/boid.obj");
        if (boid_model_.meshCount == 0) {
            TELEM_LOG_ERROR("Fatal: no boid model loaded");
            std::abort();
        } else {
            // some mesh config
            Matrix scaleMat = MatrixScale(0.05f, 0.05f, 0.05f);
            boid_model_.transform = MatrixMultiply(boid_model_.transform, scaleMat);

            // If model's forward is -Y (down) and you want +Z (world forward)
            Matrix rot = MatrixRotateX(-90 * DEG2RAD);
            boid_model_.transform = MatrixMultiply(rot, boid_model_.transform);
        }

        font_ = load_jetbrains_font();
    }

    Renderer::~Renderer()
    {
        UnloadFont(font_);
        UnloadModel(boid_model_);
        CloseWindow();
    }


    void Renderer::render()
    {
        TELEM_TRACY_ZONE_C("Render", TELEM_COLOR_RENDER);

        update_cam();

        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode3D(camera_);

        const float scale = world_.get<SimConfig>().boid_scale;

        boid_query_.each([this, scale](const ecs::Position &pos, const ecs::Heading &heading, const ecs::Hue &hue) {
            // 1. Draw the actual boid body
            Vector3 position = {pos.value.x(), pos.value.y(), pos.value.z()};
            Vector3 forward = {heading.value.x(), heading.value.y(), heading.value.z()};
            uint8_t tint = hue.value;
            draw_boid(position, forward, scale, tint);

            // TODO: Debug visualisation for boid heading and radius and vision.
        });

        const float half = world_.get<SimConfig>().world_half;

        DrawCubeWires(
            {0.0f, 0.0f, 0.0f},
            2.0f * half,
            2.0f * half,
            2.0f * half,
            RAYWHITE
        );

        EndMode3D();

        DrawFPS(20, 20);

        EndDrawing();
    }


    void Renderer::update_cam()
    {
        TELEM_TRACY_ZONE("UpdateCam");

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
            const Vector2 md = GetMouseDelta();
            camera_yaw_ -= md.x * cam_orbit_sensitivity_deg_ * DEG2RAD;
            camera_pitch_ += md.y * cam_orbit_sensitivity_deg_ * DEG2RAD;

            constexpr float maxPitch = 89.0f * DEG2RAD;
            if (camera_pitch_ > maxPitch) camera_pitch_ = maxPitch;
            if (camera_pitch_ < -maxPitch) camera_pitch_ = -maxPitch;
        }

        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            camera_distance_ -= wheel * cam_zoom_sensitivity_;
            if (camera_distance_ < cam_min_distance_) camera_distance_ = cam_min_distance_;
            if (camera_distance_ > cam_max_distance_) camera_distance_ = cam_max_distance_;
        }

        const float cosPitch = cosf(camera_pitch_);
        camera_.position.x = camera_.target.x + camera_distance_ * cosPitch * sinf(camera_yaw_);
        camera_.position.y = camera_.target.y + camera_distance_ * sinf(camera_pitch_);
        camera_.position.z = camera_.target.z + camera_distance_ * cosPitch * cosf(camera_yaw_);
    }


    bool Renderer::is_running() const
    {
        return !WindowShouldClose();
    }

#pragma region Helpers

    Camera3D Renderer::init_camera(const SimConfig &cfg)
    {
        Camera3D cam = {0};

        const float scaledCamDist = cfg.world_half * 2.f + 20.f;

        cam.position = (Vector3){scaledCamDist, scaledCamDist, scaledCamDist};
        cam.target = (Vector3){0.0f, 0.0f, 0.0f};
        cam.up = (Vector3){0.0f, 1.0f, 0.0f};
        cam.fovy = 45.0f;
        cam.projection = CAMERA_PERSPECTIVE;

        const float dx = cam.position.x - cam.target.x;
        const float dy = cam.position.y - cam.target.y;
        const float dz = cam.position.z - cam.target.z;
        camera_distance_ = sqrtf(dx * dx + dy * dy + dz * dz);

        camera_yaw_ = atan2f(dx, dz);
        camera_pitch_ = asinf(dy / camera_distance_);

        return cam;
    }

    Font Renderer::load_jetbrains_font()
    {
        return LoadFontFromMemory(
            ".ttf",
            JetBrainsMono_Regular_ttf,
            static_cast<int>(JetBrainsMono_Regular_ttf_len),
            24,
            nullptr,
            0
        );
    }

    void Renderer::text(const char *text, const Vector2 position, const float size, const Color tint) const
    {
        DrawTextEx(
            font_,
            text,
            position,
            size,
            1,
            tint
        );
    }

    void Renderer::draw_boid(const Vector3 &pos, const Vector3 &heading, float scale, const uint8_t hue) const
    {
        // Normalise heading for security
        Vector3 forward = heading;
        float len = sqrtf(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
        if (len > 0.0001f) {
            forward.x /= len;
            forward.y /= len;
            forward.z /= len;
        } else {
            forward = {0.0f, 0.0f, 1.0f};
        }

        // World up vector
        Vector3 up = {0.0f, 1.0f, 0.0f};

        // Compute rotation axis and angle to align model's forward (+Z) with heading
        Vector3 axis;
        float angle;

        // Avoid gimbal lock when heading is almost vertical
        float dot = forward.x * up.x + forward.y * up.y + forward.z * up.z;
        if (fabsf(dot + 1.0f) < 0.0001f) {
            // Heading points straight down
            axis = {1.0f, 0.0f, 0.0f};
            angle = PI;
        } else if (fabsf(1.0f - dot) < 0.0001f) {
            // Heading points straight up
            axis = {1.0f, 0.0f, 0.0f};
            angle = 0.0f;
        } else {
            axis = Vector3CrossProduct(up, forward);
            axis = Vector3Normalize(axis);
            angle = acosf(dot);
        }

        // Convert angle to degrees for raylib
        float angleDeg = angle * RAD2DEG;

        Color tint = hue_to_color(hue, 255, 180);
        DrawModelEx(boid_model_, pos, axis, angleDeg, {scale, scale, scale}, tint);
    }

    Color Renderer::hue_to_color(uint8_t hue, uint8_t saturation, uint8_t value) const
    {
        float h = (hue / 255.0f) * 360.0f;
        float s = saturation / 255.0f;
        float v = value / 255.0f;

        float c = v * s;
        float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
        float m = v - c;

        float r, g, b;
        if (h < 60) {
            r = c;
            g = x;
            b = 0;
        } else if (h < 120) {
            r = x;
            g = c;
            b = 0;
        } else if (h < 180) {
            r = 0;
            g = c;
            b = x;
        } else if (h < 240) {
            r = 0;
            g = x;
            b = c;
        } else if (h < 300) {
            r = x;
            g = 0;
            b = c;
        } else {
            r = c;
            g = 0;
            b = x;
        }

        // Convert 0-1 range to 0-255 and add m
        return {
            static_cast<uint8_t>((r + m) * 255),
            static_cast<uint8_t>((g + m) * 255),
            static_cast<uint8_t>((b + m) * 255),
            255
        };
    }


#pragma endregion
}

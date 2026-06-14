#include "renderer.hpp"

#include <cmath>
#include <raylib.h>

#include "../core/ecs/components.hpp"
#include "config.hpp"
#include "telemetry.hpp"
#include "assets/font_jetbrains.h"

namespace simnet::client {
    Renderer::Renderer(const int width, const int height, const std::string_view title, const flecs::world &world)
        : world_(world),
          width_(width),
          height_(height),
          boid_query_(
              world.query_builder<const ecs::Position, const ecs::Heading>().with<ecs::Boid>().cached().build()),
          camera_distance_(0),
          camera_yaw_(0),
          camera_pitch_(0),
          camera_(init_camera())
    {
        int flags = FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE;

        // SetConfigFlags(flags);
        SetTargetFPS(0);
        SetExitKey(0);
        SetTraceLogLevel(LOG_WARNING);

        InitWindow(width_, height_, title.data());
        // Fatal issue with raylib
        if (!IsWindowReady()) {
            TELEM_LOG_ERROR("Fatal: unable to initialise OpenGL window");
            std::abort();
        }

        font_ = load_jetbrains_font();
    }

    Renderer::~Renderer()
    {
        UnloadFont(font_);
        CloseWindow();
    }


    void Renderer::render()
    {
        TELEM_TRACY_ZONE_C("Render", TELEM_COLOR_RENDER);

        update_cam();

        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode3D(camera_);

        boid_query_.each([](const ecs::Position &pos, const ecs::Heading &heading) {
            // 1. Draw the actual boid body
            DrawSphere(
                {pos.value.x(), pos.value.y(), pos.value.z()},
                config::BOID_SCALE,
                BLUE
            );

            // TODO: Debug visualisation for boid heading and radius and vision.
        });


        DrawCubeWires(
            {0.0f, 0.0f, 0.0f},
            2.0f * config::WORLD_HALF,
            2.0f * config::WORLD_HALF,
            2.0f * config::WORLD_HALF,
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

    Camera3D Renderer::init_camera()
    {
        Camera3D cam = {0};

        constexpr float scaledCamDist = config::WORLD_HALF * 2.f + 20.f;

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

#pragma endregion
}

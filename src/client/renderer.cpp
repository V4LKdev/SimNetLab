#include "renderer.hpp"

#include <cmath>
#include <raylib.h>

#include "components.hpp"
#include "config.hpp"
#include "assets/font_jetbrains.h"

namespace simnet::client {

    Renderer::Renderer(int width, int height, std::string_view title, const flecs::world& world)
        :   world_(world),
            width_(width),
            height_(height),
            camera_(init_camera())
    {
    #ifdef NDEBUG
        SetTraceLogLevel(LOG_TRACE);
    #else
        SetTraceLogLevel(LOG_ERROR);
    #endif

        int flags = FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE;

        // SetConfigFlags(flags);
        SetTargetFPS(0);
        SetExitKey(0);

        InitWindow(width_, height_, title.data());
        if (!IsWindowReady()) {
            TraceLog(LOG_ERROR, "Unable to initialize OpenGL context");
            return;
        }

        font_ = load_jetbrains_font();
    }

    Renderer::~Renderer() {
        UnloadFont(font_);
        CloseWindow();
    }


    void Renderer::render()
    {
        update_cam();

        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode3D(camera_);

        world_.query<const ecs::Position3D, const ecs::Boid>().each(
            [](const ecs::Position3D& position, const ecs::Boid& boid)
            {
                DrawSphere({position.x, position.y, position.z}, config::BOID_SCALE, BLUE);
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
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            Vector2 md = GetMouseDelta();
            camera_yaw_   -= md.x * cam_orbit_sensitivity_deg_ * DEG2RAD;
            camera_pitch_ += md.y * cam_orbit_sensitivity_deg_ * DEG2RAD;

            const float maxPitch = 89.0f * DEG2RAD;
            if (camera_pitch_ >  maxPitch) camera_pitch_ =  maxPitch;
            if (camera_pitch_ < -maxPitch) camera_pitch_ = -maxPitch;
        }

        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            camera_distance_ -= wheel * cam_zoom_sensitivity_;
            if (camera_distance_ < cam_min_distance_) camera_distance_ = cam_min_distance_;
            if (camera_distance_ > cam_max_distance_) camera_distance_ = cam_max_distance_;
        }

        float cosPitch = cosf(camera_pitch_);
        camera_.position.x = camera_.target.x + camera_distance_ * cosPitch * sinf(camera_yaw_);
        camera_.position.y = camera_.target.y + camera_distance_ * sinf(camera_pitch_);
        camera_.position.z = camera_.target.z + camera_distance_ * cosPitch * cosf(camera_yaw_);
    }


    bool Renderer::is_running()
    {
        return !WindowShouldClose();
    }

#pragma region Helpers

    Camera3D Renderer::init_camera()
    {
        Camera3D cam = { 0 };
        cam.position = (Vector3){1100.f, 1100.0f, 1100.0f};
        cam.target = (Vector3){0.0f, 0.0f, 0.0f};
        cam.up = (Vector3){0.0f, 1.0f, 0.0f};
        cam.fovy = 45.0f;
        cam.projection = CAMERA_PERSPECTIVE;

        float dx = cam.position.x - cam.target.x;
        float dy = cam.position.y - cam.target.y;
        float dz = cam.position.z - cam.target.z;
        camera_distance_ = sqrtf(dx*dx + dy*dy + dz*dz);

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

    void Renderer::text(const char *text, Vector2 position, float size, Color tint) const
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

#include "renderer.hpp"

#include <raylib.h>

#include "components.hpp"
#include "assets/font_jetbrains.h"

namespace simnet::client {

    Renderer::Renderer(int width, int height, std::string_view title, const flecs::world& world)
        :   width_(width),
            height_(height),
            camera_(init_camera()),
            world_(world)
    {
        InitWindow(width_, height_, title.data());
        SetTargetFPS(0);

        font_ = LoadFontFromMemory(
            ".ttf",
            JetBrainsMono_Regular_ttf,
            static_cast<int>(JetBrainsMono_Regular_ttf_len),
            24,
            nullptr,
            0
        );
    }

    Renderer::~Renderer() {
        UnloadFont(font_);
        CloseWindow();
    }

    void Renderer::begin_frame()
    {
        BeginDrawing();
        ClearBackground(RAYWHITE);
    }

    void Renderer::end_frame()
    {
        EndDrawing();
    }

    void Renderer::draw_world() const
    {

        BeginMode3D(camera_);

        world_.query<const simnet::ecs::Position3D, const ecs::Renderable>().each(
            [](const ecs::Position3D& pos, const ecs::Renderable&) {
               DrawSphere({pos.x, pos.y, pos.z}, 0.2f, DARKBLUE);
            });

        DrawGrid(10, 1.0f);

        EndMode3D();
    }

    void Renderer::draw_debug(int steps, double alpha, uint64_t tick)
    {

        Text(
            TextFormat("steps: %d", steps),
            {20.f, 20.f},
            24,
            BLUE);

        Text(
            TextFormat("alpha: %f", alpha),
            {20.f, 50.f},
            24,
            BLUE
        );

        Text(
            TextFormat("tick: %llu", tick),
            {20.f, 80.f},
            24,
            BLUE
        );

        DrawFPS(20, 120);
    }


    bool Renderer::is_running()
    {
        return !WindowShouldClose();
    }

    Camera3D Renderer::init_camera()
    {
        Camera3D cam = { 0 };
        cam.position = (Vector3){0.0f, 10.0f, 10.0f};
        cam.target = (Vector3){0.0f, 0.0f, 0.0f};
        cam.up = (Vector3){0.0f, 1.0f, 0.0f};
        cam.fovy = 45.0f;
        cam.projection = CAMERA_PERSPECTIVE;
        return cam;
    }

    void Renderer::Text(const char *text, Vector2 position, float size, Color tint) const
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
}

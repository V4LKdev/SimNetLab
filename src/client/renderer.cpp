#include "renderer.hpp"

#include <raylib.h>

#include "assets/font_jetbrains.h"

namespace simnet::client {

    Renderer::Renderer(int width, int height, std::string_view title)
        :   width_(width),
            height_(height)
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

    void Renderer::begin_frame() {
        BeginDrawing();
        ClearBackground(DARKGRAY);
    }

    void Renderer::end_frame() {
        EndDrawing();
    }

    void Renderer::draw_debug(int steps, double alpha, uint64_t tick) {

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
    }


    bool Renderer::is_running() {
        return !WindowShouldClose();
    }

    void Renderer::Text(const char *text, Vector2 position, float size, Color tint) const {
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

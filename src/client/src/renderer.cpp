#include "renderer.hpp"

#include <raylib.h>

namespace simnet::client {

    Renderer::Renderer(int width, int height, std::string_view title)
        :   width_(width),
            height_(height)
    {
        InitWindow(width_, height_, title.data());
        SetTargetFPS(0);
    }

    Renderer::~Renderer() {
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
        DrawText(TextFormat("steps: %d", steps), 20, 20, 24, BLUE);
        DrawText(TextFormat("alpha: %f", alpha), 20, 50, 24, BLUE);
        DrawText(TextFormat("tick: %llu", tick), 20, 80, 24, BLUE);
    }

    bool Renderer::is_running() {
        return !WindowShouldClose();
    }
}

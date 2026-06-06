#pragma once
#include <cstdint>
#include <raylib.h>
#include <string_view>
#include <flecs.h>

namespace simnet::client {

    class Renderer {
    public:

        Renderer(int width, int height, std::string_view title, const flecs::world& world);
        ~Renderer();

        Renderer& operator=(const Renderer&) = delete;
        Renderer(const Renderer&) = delete;
        Renderer(Renderer&&) = delete;
        Renderer& operator=(Renderer&&) = delete;


        void begin_frame();
        void end_frame();

        void draw_world() const;
        void draw_debug(int steps, double alpha, uint64_t tick);

        bool is_running();

    private:
        Camera3D init_camera();

        void Text(const char* text, Vector2 position, float size, Color tint) const;

        const flecs::world& world_;
        int width_;
        int height_;
        Font font_ = GetFontDefault();
        Camera3D camera_;
    };

}


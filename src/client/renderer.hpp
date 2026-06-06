#pragma once
#include <cstdint>
#include <raylib.h>
#include <string_view>

namespace simnet::client {

    class Renderer {
    public:

        Renderer(int width, int height, std::string_view title);
        ~Renderer();

        Renderer& operator=(const Renderer&) = delete;
        Renderer(const Renderer&) = delete;
        Renderer(Renderer&&) = delete;
        Renderer& operator=(Renderer&&) = delete;


        void begin_frame();
        void end_frame();

        void draw_debug(int steps, double alpha, uint64_t tick);

        bool is_running();

    private:
        void Text(const char* text, Vector2 position, float size, Color tint) const;

        int width_;
        int height_;
        Font font_ = GetFontDefault();
    };

}


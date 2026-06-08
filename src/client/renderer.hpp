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

        void render();

        static bool is_running();

    private:
        Camera3D init_camera();
        void update_cam();
        static Font load_jetbrains_font();
        void text(const char* text, Vector2 position, float size, Color tint) const;


        const flecs::world& world_;
        int width_;
        int height_;
        Font font_ = GetFontDefault();
        Camera3D camera_;

        float camera_distance_;
        float camera_yaw_, camera_pitch_;
        const float cam_min_distance_ = 10.f;
        const float cam_max_distance_ = 2000.f;
        const float cam_zoom_sensitivity_ = 25.f;
        const float cam_orbit_sensitivity_deg_ = 0.25f;
    };

}


#pragma once
#include <raylib.h>
#include <string_view>
#include <flecs.h>

#include "ecs/components.hpp"

namespace simnet::client {
    /*
     *  Client-side renderer using raylib.
     */
    class Renderer {
    public:
        Renderer(int width, int height, std::string_view title, const flecs::world &world);

        ~Renderer();

        Renderer &operator=(const Renderer &) = delete;

        Renderer(const Renderer &) = delete;

        Renderer(Renderer &&) = delete;

        Renderer &operator=(Renderer &&) = delete;

        /// Draw one frame (updates cam, clears, renders entities)
        void render();

        /// True while the raylib window is open.
        bool is_running() const;

    private:
        Camera3D init_camera();

        void update_cam();

        [[nodiscard]]
        static Font load_jetbrains_font();

        void text(const char *text, Vector2 position, float size, Color tint) const;


        const flecs::world &world_;
        int width_;
        int height_;
        Font font_ = GetFontDefault();

        flecs::query<const ecs::Position, const ecs::Heading> boid_query_;

        float camera_distance_;
        float camera_yaw_, camera_pitch_;
        Camera3D camera_;

        static constexpr float cam_min_distance_ = 10.f;
        static constexpr float cam_max_distance_ = 2000.f;
        static constexpr float cam_zoom_sensitivity_ = 25.f;
        static constexpr float cam_orbit_sensitivity_deg_ = 0.25f;
    };
}


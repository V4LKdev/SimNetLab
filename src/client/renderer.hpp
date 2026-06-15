#pragma once
#include <raylib.h>
#include <string_view>
#include <flecs.h>

#include "../core/config/SimConfig.hpp"
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
        Camera3D init_camera(const SimConfig &cfg);

        void update_cam();

        [[nodiscard]]
        static Font load_jetbrains_font();

        void text(const char *text, Vector2 position, float size, Color tint) const;

        void draw_boid(const Vector3 &pos, const Vector3 &heading, float scale, const uint8_t hue) const;

        Color hue_to_color(uint8_t hue, uint8_t saturation = 255, uint8_t value = 255) const;

        Model boid_model_ = {0};

        const flecs::world &world_;
        int width_;
        int height_;
        Font font_ = GetFontDefault();

        flecs::query<const ecs::Position, const ecs::Heading, const ecs::Hue> boid_query_;

        float camera_distance_;
        float camera_yaw_, camera_pitch_;
        Camera3D camera_;

        static constexpr float cam_min_distance_ = 10.f;
        static constexpr float cam_max_distance_ = 2000.f;
        static constexpr float cam_zoom_sensitivity_ = 25.f;
        static constexpr float cam_orbit_sensitivity_deg_ = 0.25f;
    };
}


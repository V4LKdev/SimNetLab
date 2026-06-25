#pragma once
#include <memory>
#include <flecs.h>

#include "app_context.hpp"
#include "core/core.hpp"

namespace simnet::game::server {
    /**
    * @brief Owns the server‑side ECS world, its systems, and the simulation state.
    */
    class ServerWorld {
    public:
        ServerWorld(config::SimConfig config, net::NetManager *net = nullptr);

        ~ServerWorld();

        // ------------------ ownership rules ------------------
        ServerWorld(const ServerWorld &) = delete;

        ServerWorld &operator=(const ServerWorld &) = delete;

        ServerWorld(ServerWorld &&) = delete;

        ServerWorld &operator=(ServerWorld &&) = delete;


        /**
         * @brief Advance the simulation by one fixed timestep.
         */
        void run_tick(float dt);

        /**
         * @brief Return a reference to the underlying flecs world.
         */
        flecs::world &world() noexcept { return world_; }

        [[nodiscard]]
        const flecs::world &world() const noexcept { return world_; }

    private:
        void register_components();

        void build_network_pipeline();

        void register_server_systems();

        void configure_threads();

        flecs::world world_;
        std::unique_ptr<AppContext> ctx_;
        net::NetManager *net_ = nullptr;
        config::SimConfig config_;
    };
}

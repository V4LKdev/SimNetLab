#pragma once

#include <chrono>
#include <memory>
#include <flecs.h>

namespace simnet::core::net {
    class NetManager;
    struct NetConfig;
}

namespace simnet::core::config {
    struct SimConfig;
}

namespace simnet::core::utils {
    using TimePoint = std::chrono::steady_clock::time_point;
}

namespace simnet::game::server {
    /**
     * @brief Owns the server‑side ECS world and the network manager.
     *
     * The main loop drives this class via update(), which processes
     * network events and advances the simulation with a fixed timestep.
     */
    class ServerWorld {
    public:
        ServerWorld();

        ~ServerWorld();

        ServerWorld(const ServerWorld &) = delete;

        ServerWorld &operator=(const ServerWorld &) = delete;

        ServerWorld(ServerWorld &&) = delete;

        ServerWorld &operator=(ServerWorld &&) = delete;

        /**
         * @brief Initialise network and ECS world.
         * @return true on success.
         */
        bool initialize(const core::net::NetConfig &net_cfg,
                        const core::config::SimConfig &sim_cfg);

        /**
         * @brief Process incoming network events and advance simulation.
         * @param now Monotonic timestamp for this iteration.
         */
        void update(core::utils::TimePoint now);

        /** @brief Cleanly shut down network and ECS. */
        void shutdown();

        /** @brief Expose the underlying flecs world (read‑only view). */
        [[nodiscard]] const flecs::world &world() const noexcept;

    private:
        void init_ecs(const core::config::SimConfig &cfg);

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

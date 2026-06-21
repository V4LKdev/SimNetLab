#pragma once
#include <flecs.h>
#include <memory>
#include <atomic>



namespace simnet::client {
    class RenderSystem;
    class NetworkClient;

    class ClientWorld {
    public:
        /// Initialize the world, register ECS components/systems, create the renderer and network
        ClientWorld(const SimConfig &cfg);

        ~ClientWorld();

        ClientWorld(const ClientWorld &) = delete;

        ClientWorld &operator=(const ClientWorld &) = delete;

        ClientWorld(ClientWorld &&) = delete;

        ClientWorld &operator=(ClientWorld &&) = delete;

        /// Run the main loop (blocks until window close or signal).
        void Run();

    private:
        void register_snapshot_system();


        flecs::world world_;
        std::unique_ptr<RenderSystem> renderer_;
        std::unique_ptr<NetworkClient> network_;

        static std::atomic<bool> s_running;
    };
}

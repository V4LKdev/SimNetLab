#pragma once
#include <flecs.h>

namespace simnet {
    struct SimConfig;
}

namespace simnet::client {

    class ClientWorld {
        public:
        explicit ClientWorld(const SimConfig& cfg);
        ~ClientWorld();

        ClientWorld(const ClientWorld&) = delete;
        ClientWorld& operator=(const ClientWorld&) = delete;
        ClientWorld(ClientWorld&&) = delete;
        ClientWorld& operator=(ClientWorld&&) = delete;

        /// Access the ECS world
        [[nodiscard]]
        const flecs::world& world() const {return world_;}

        /// Tick client-side systems
        void update();

        private:
        flecs::world world_;
    };

}

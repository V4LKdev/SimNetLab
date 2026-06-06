#pragma once
#include <cstdint>

namespace simnet::sim {

    class Simulation {

    public:
        void step();

        [[nodiscard]]
        uint64_t current_tick() const;

    private:
        uint64_t tick_ = 0;
    };

}
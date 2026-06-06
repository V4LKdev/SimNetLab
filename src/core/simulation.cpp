#include "simulation.hpp"

namespace simnet::sim {

    void Simulation::step()
    {
        ++tick_;
    }

    uint64_t Simulation::current_tick() const
    {
        return tick_;
    }
}
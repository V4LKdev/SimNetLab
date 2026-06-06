#include "simulation.hpp"

void simnet::sim::Simulation::step()
{
    ++tick_;
}

uint64_t simnet::sim::Simulation::current_tick() const
{
    return tick_;
}

// =============================================================================
// Core Module Aggregate Header - Public API Only
// =============================================================================
#pragma once

// --- Config ------------------------------------------------------------------
#include "config/config_loader.hpp"
#include "config/sim_config.hpp"

// --- ECS Infrastructure ------------------------------------------------------
#include "ecs/world_factory.hpp"

// --- Math --------------------------------------------------------------------
#include "math/vec3.hpp"
#include "math/rules_scalar.hpp"

// --- Utilities ---------------------------------------------------------------
#include "utils/controller.hpp"
#include "utils/id_generator.hpp"
#include "utils/time_keeper.hpp"

// --- Network Facade ----------------------------------------------------------
#include "net/net_manager.hpp"

// =============================================================================
// Namespace Aliases (convenience for downstream consumers)
// =============================================================================
namespace simnet {
    namespace net = core::net;
    namespace math = core::math;
    namespace ecs = core::ecs;
    namespace config = core::config;
    namespace utils = core::utils;
}

// src/core/prelude.hpp
#pragma once

// =============================================================================
// Core Module Prelude – Public API Only
// =============================================================================

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

// --- Network Facade (only the public interface) ------------------------------
#include "net/net_interface.hpp"

// =============================================================================
// Namespace Aliases
// =============================================================================
namespace simnet {
    namespace net = core::net;
    namespace math = core::math;
    namespace ecs = core::ecs;
    namespace config = core::config;
    namespace utils = core::utils;
}

// =============================================================================
// Game Shared Aggregate Header - Logic that runs on both client and server
// =============================================================================
#pragma once

// --- ECS Data ----------------------------------------------------------------
#include "game/shared/components.hpp"

// --- Boid Rules --------------------------------------------------------------
#include "game/shared/rules/rules_scalar.hpp"

// --- Shared Game Systems -----------------------------------------------------
#include "game/shared/flocking_systems.hpp"

// --- Telemetry (optional at compile time, guarded internally) ----------------
#include "game/shared/telemetry_systems.hpp"

// =============================================================================
// Namespace Aliases
// =============================================================================
namespace simnet {
    namespace shared = game::shared;
}

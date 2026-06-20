// =============================================================================
// Game Shared Aggregate Header - Logic that runs on both client and server
// =============================================================================
#pragma once

// Bring in the core engine API
#include "core/core.hpp"

// --- ECS Data ----------------------------------------------------------------
#include "game/shared/components.hpp"

// --- Shared Game Systems -----------------------------------------------------
#include "game/shared/flocking_systems.hpp"

// --- Telemetry (optional at compile time, guarded internally) ----------------
#include "game/shared/telemetry_systems.hpp"

// =============================================================================
// Namespace Aliases
// =============================================================================
namespace simnet {
    namespace shared = game::shared;
    // 'net', 'math', etc. are already aliased via core/core.hpp
}

// src/game/shared/prelude.hpp
#pragma once

// =============================================================================
// Game Shared Prelude – Logic that runs on both client and server
// =============================================================================

#include "prelude.hpp"

// --- ECS Data -----------------------------------------------------------------
#include "components.hpp"
#include "tags.hpp"

// --- Network Translation (pure data conversion, no net dependency) -----------
#include "network_translation.hpp"

// --- Shared Game Systems ------------------------------------------------------
#include "flocking_systems.hpp"
#include "telemetry_systems.hpp"

// =============================================================================
// Namespace Aliases
// =============================================================================

namespace simnet {
    namespace shared = game::shared;
    // 'net', 'math', etc. are already aliased via core/prelude.hpp
}

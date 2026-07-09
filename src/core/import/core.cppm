/**
 * @defgroup core simnet.core
 * @brief Dependency‑free core vocabulary.
 *
 * @details
 * This umbrella module re‑exports three private sub‑modules.
 *
 * @note All types are PoD. No heap allocations or third-party imports.
 */
export module simnet.core;

/**
 * @par simnet.core:bytes
 * Strongly‑typed network identifiers (EntityNetId, PeerId, SequenceId)
 * and byte views (Byte, ByteSpan, MutableByteSpan).
 */
export import :bytes;

/**
 * @par simnet.core:math
 * Three‑component float vector (Vec3f) and axis‑aligned bounding box (Aabb3f)
 * with arithmetic operators, dot product, length, normalisation, and
 * containment tests. All operations are `constexpr` except `length()` and
 * `normalize_or()`, which rely on `std::sqrt`.
 */
export import :math;

/**
 * @par simnet.core:time
 * Fixed‑step simulation state (Tick, FixedStepSettings, FixedStepClock),
 * factory function `make_clock`, and a function to compute the fixed
 * delta‑time from a tick rate. The timestep is rounded to the nearest
 * nanosecond to minimise long‑term drift. The clock is a mutable PoD;
 * `make_clock` should be used to initialise it correctly.
 */
export import :time;

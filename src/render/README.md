# simnet_render

`simnet_render` is a generic Raylib viewer for caller-owned entity SoA data. Its public API depends only on `simnet_core`. Raylib is a private implementation dependency.

The private panel uses the embedded JetBrains Mono Regular font from
`assets/jetbrains_mono_regular.hpp`. JetBrains Mono is licensed under Apache-2.0.

`RenderEntityView` does not own state. Its identifier, position, heading, and hue spans must remain valid for the duration of `Viewer::draw()`. Mismatched span lengths reject the entity frame without reading it. Entities with non-finite position or heading values are skipped during preparation.

The Phase 1 viewer creates one fixed `1800 x 1080` window per process. A `360` pixel Raylib panel occupies the left side. The right `1440 x 1080` region draws a 4:3 scene render texture. The procedural wedge mesh points along local `+Z` with local `+Y` up.

The viewer uses 32 persistent hue buckets and one instanced draw per non-empty bucket. It renders world bounds, optional axes, and an overview orbit camera. Right drag orbits, the wheel zooms, and `R` resets the active camera. The Server viewer can pause locally. A ready Client viewer requests an authoritative pause state and continues applying snapshots while paused.

The Viewer owns three local panel pages. `F1` shows overview and rendering facts, `F2` shows optional connection and replication facts, and `F3` shows the selected entity. `H` or `?` opens the scene help overlay with the complete control list.

Left click in the scene viewport performs a nearest-hit ray-to-sphere selection using the configured picking radius. The Viewer stores the stable `EntityNetId`, not a frame-local array index. A hit enters Entity Detail mode with an independent orbit camera and a wire highlight. Empty scene clicks preserve the current selection. `Backspace` or Return to overview clears it. If the selected ID is absent from a later valid frame, the Viewer clears it and returns to Overview. `[` and `]` select the previous and next valid visible IDs with wrapping. Optional `SelectedEntityDetails` are shown only when their ID matches the current selection.

The viewer returns user intent and aggregate CPU timings. It does not own simulation, pause state, transport, snapshots, Flecs, telemetry, or configuration loading.

# simnet_render

`simnet_render` is a generic Raylib viewer for caller-owned entity SoA data. Its public API depends only on `simnet_core`. Raylib is a private implementation dependency.

The private panel uses the embedded JetBrains Mono Regular font from
`assets/jetbrains_mono_regular.hpp`. JetBrains Mono is licensed under Apache-2.0.

`RenderEntityView` does not own state. Its identifier, position, heading, and hue spans must remain valid for the duration of `Viewer::draw()`. Mismatched span lengths reject the entity frame without reading it. Entities with non-finite position or heading values are skipped during preparation.

The Phase 1 viewer creates one fixed `1800 x 1080` window per process. A `360` pixel Raylib panel occupies the left side. The right `1440 x 1080` region draws a 4:3 scene render texture. The procedural wedge mesh points along local `+Z` with local `+Y` up.

The viewer uses 32 persistent hue buckets and one instanced draw per non-empty bucket. It renders world bounds, optional axes, and an overview orbit camera. Right drag orbits, the wheel zooms, and `R` resets the active camera. The Server viewer can pause locally. A ready Client viewer requests an authoritative pause state and continues applying snapshots while paused.

`ViewerConfig::entity_mesh_path` optionally selects an OBJ model loaded once during Viewer construction. Every mesh in a loaded model uses the same instanced hue buckets. An empty or failed path uses the procedural wedge, which points along local `+Z` with local `+Y` up. The reference `boid.obj` already uses local `+Z` forward and centimeter-sized coordinates, so the private loader bakes its static scale into mesh vertices once. The caller's `entity_scale` remains the final visual scale.

The Viewer owns three local panel pages. `F1` shows overview and rendering facts, `F2` shows optional connection and replication facts, and `F3` shows the selected entity. `H` or `?` opens the scene help overlay with the complete control list.

Applications may supply presentation interpolation facts for F1. The renderer still consumes one already-resolved entity view and has no snapshot-history policy. Server and Client application code interpolate presentation snapshots before `Viewer::draw()`.

Applications can supply a local `ObserverView` without giving the renderer any simulation or networking ownership. `F4` switches between the debug orbit camera and the observer camera. Arrow keys rotate the application-owned observer in either view. The observer uses one vertical FOV, and the renderer derives the matching horizontal FOV from the 4:3 scene aspect. Overview can display its marker, forward line, interest sphere, and frustum.

Applications can also supply bounded occupied-cell data through `SpatialDebugView`. The renderer draws only supplied cell bounds and never imports `simnet_spatial`. The Server currently rebuilds this view from its authoritative render snapshot and uses the configured shared spatial cell size.

Interpolated entity meshes may trail authoritative Server spatial cells and rule data by at most one simulation tick. Selected-boid vector origins use the displayed interpolated position so the gizmos remain readable.

Left click in the scene viewport performs a nearest-hit ray-to-sphere selection using the configured picking radius. The Viewer stores the stable `EntityNetId`, not a frame-local array index. A hit enters Entity Detail mode with an independent orbit camera and a wire highlight. Empty scene clicks preserve the current selection. `Backspace` or Return to overview clears it. If the selected ID is absent from a later valid frame, the Viewer clears it and returns to Overview. `[` and `]` select the previous and next valid visible IDs with wrapping. Optional `SelectedEntityDetails` are shown only when their ID matches the current selection. The authoritative Server can supply velocity, acceleration, neighbour counts, and steering contributions without giving the renderer simulation ownership.

The viewer returns user intent and aggregate CPU timings. It does not own simulation, pause state, transport, snapshots, Flecs, telemetry, or configuration loading.

The public render API remains telemetry independent. Its private implementation emits aggregate Tracy zones and plots through `simnet_telemetry` when the build enables Tracy. It does not trace per entity or per cell.

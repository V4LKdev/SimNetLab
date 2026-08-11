# simnet_render

`simnet_render` is a generic Raylib viewer for caller-owned entity SoA data. Its public API depends only on `simnet_core`. Raylib is a private implementation dependency.

The implementation is divided by responsibility. `render_viewer.cpp` owns
window/resource lifetime, cameras, input, and selection.
`render_viewer_scene.cpp` prepares and submits entity and debug geometry, and
`render_ui_model.cpp` builds fixed-capacity inspector sections without recurring
heap allocation. `render_viewer_panel.cpp` owns sidebar layout, scrolling, and
UI interaction. `render_viewer_viewport_ui.cpp` draws the
toolbar menus, contextual help, selection card, and orientation gizmo. The shared
private implementation header contains only Viewer state and internal helpers.
It is not part of the module's public API.

Viewer construction releases every acquired Raylib resource if a later initialization step fails.
Normal destruction releases the same resources in reverse ownership order.

The private panel uses the tracked JetBrains Mono Nerd Font Regular resource
from `assets/JetBrainsMonoNerdFont-Regular.ttf`. The viewer loads a bounded
ASCII and interface-icon glyph set at startup rather than building an atlas for
the entire patched font. The font is distributed under the SIL Open Font
License 1.1 in `assets/JetBrainsMonoNerdFont-OFL.txt`.

`RenderEntityView` does not own state. Its identifier, position, heading, and hue spans must remain valid for the duration of `Viewer::draw()`. Mismatched span lengths reject the entity frame without reading it. Entities with non-finite position or heading values are skipped during preparation.

The viewer creates one fixed `1800 x 1080` window per process. A `420` pixel
Raylib panel provides room for the 16-pixel body type scale. The remaining
`1380 x 1080` region draws the scene render texture. The procedural wedge mesh
points along local `+Z` with local `+Y` up.

The viewer uses 32 persistent hue buckets and one instanced draw per non-empty bucket. It renders world bounds, optional world-origin axes, a permanent camera-relative orientation gizmo, and an overview orbit camera. Right drag orbits, the wheel zooms, and `R` resets the active camera. The Server viewer can pause locally. A ready Client viewer requests an authoritative pause state and continues applying snapshots while paused.

`ViewerConfig::entity_mesh_path` optionally selects an OBJ model loaded once during Viewer construction. Every mesh in a loaded model uses the same instanced hue buckets. An empty or failed path uses the procedural wedge, which points along local `+Z` with local `+Y` up. The reference `boid.obj` already uses local `+Z` forward and centimeter-sized coordinates, so the private loader bakes its static scale into mesh vertices once. The caller's `entity_scale` remains the final visual scale.

Inspector page and camera mode are independent. `F1` shows selective runtime,
world, spatial, presentation, and viewer-performance facts. `F2` shows
capability-aware connection and replication facts. `F3` shows selected-entity
behavior with deeper data behind Advanced Diagnostics. `F4` shows the
effective read-only experiment Setup.
Each page retains its own scroll position. The fixed header and passive footer
remain visible while the information region scrolls.

The viewport toolbar exposes pause, an explicit camera menu, visual overlays,
and contextual help. `P`, `C`, `M`, and `H` provide the matching shortcuts.
`Escape` retains its Raylib meaning and quits the application. Overlay availability
comes from producer capabilities. Bounds, world-origin axes, spatial cells, stationary observer
geometry, selected marker, rule radii, steering vectors, queried cells, FOV,
trail, and bounded debug labels remain viewer-local presentation choices.
Only one toolbar popover can be open at a time.

`RunSetupView` is a generic non-owning section/row contract. Server and Client
applications format it once from the effective shared and local configuration,
resolved pipeline definition, and fingerprints. The renderer neither reparses
JSON nor imports configuration or pipeline types. Disabled techniques remain
visible on Setup because they define the experimental condition. Transient
packet outcomes remain Network-page data.

Applications may supply presentation interpolation facts for F1. The renderer still consumes one already-resolved entity view and has no snapshot-history policy. Server and Client application code interpolate presentation snapshots before `Viewer::draw()`.

Applications can optionally supply either a local `StationaryObserverView` or an application-resolved `GameCameraView` without giving the renderer simulation, role, or networking ownership. `C` opens a menu containing only the available cameras. Arrow keys rotate the application-owned stationary observer. Its vertical FOV determines the matching horizontal FOV for the scene aspect. Overview can display its marker, forward line, interest sphere, and frustum. The Server supplies neither special camera. Future peer interest sources belong in application-provided debug overlays, not Server camera modes.

A Client may instead supply a resolved `GameCameraView`. The generic viewer uses its position, target, up vector, and FOV without knowing about replication or player authority. Game emits semantic key/button state through `ViewerResult`. The application owns the locked chase-camera calculation and maps those inputs to its protocol. A missing camera pose falls back to Overview Orbit.

Applications can also supply bounded occupied-cell data through `SpatialDebugView`. The renderer draws only supplied cell bounds and never imports `simnet_spatial`. The Server currently rebuilds this view from its authoritative render snapshot and uses the configured shared spatial cell size.

Interpolated entity meshes may trail authoritative Server spatial cells and rule data by at most one simulation tick. Selected-boid vector origins use the displayed interpolated position so the gizmos remain readable.

Left click in the scene viewport performs a nearest-hit ray-to-sphere selection using the configured picking radius. The Viewer stores the stable `EntityNetId`, not a frame-local array index. A hit opens the Entity inspector and enters Entity Follow with an independent orbit camera and a wire highlight. F1/F2/F4 may then change inspector context without changing the camera. `Backspace` clears the selection and returns both concerns to Overview. `[` and `]` select the previous and next valid visible IDs with wrapping. Optional `SelectedEntityDetails` are shown only when their ID matches the current selection. The authoritative Server can supply velocity, acceleration, neighbor counts, and steering contributions without giving the renderer simulation ownership.

The Viewer also keeps a presentation-only trail for the selected entity. It samples the already-resolved displayed position after meaningful movement, retains at most 2,400 points in a deque, and submits the fading path as one line batch. Changing or clearing selection resets it. Paused frames do not add duplicate points. The overlay menu can hide the trail without discarding its bounded history. This state never feeds simulation, snapshots, networking, or spatial queries.

The normal inspector deliberately omits renderer implementation counters,
static configuration, and normal zero/false states. Conditional warnings appear
only when relevant. Full vectors, spatial query internals, hue details, and
retained replication history live behind Advanced Diagnostics. Viewer CPU
reports completed previous-frame viewer work and excludes `EndDrawing`
presentation wait. `ViewerResult` returns the completed current frame. Missing optionals remain distinct from real zeroes.
The private constexpr palette provides restrained surface, text, accent,
success, warning, error, and selection colors.

The viewer returns user intent and aggregate CPU timings. It does not own simulation, pause state, transport, snapshots, Flecs, telemetry, or configuration loading.

The public render API remains telemetry independent. Its private implementation emits aggregate Tracy zones and plots through `simnet_telemetry` when the build enables Tracy. It does not trace per entity or per cell.

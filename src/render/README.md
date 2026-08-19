# simnet_render

## Purpose and boundary

`simnet_render` is a generic Raylib viewer for caller-owned structure-of-arrays entity data. Its
public API depends only on `simnet_core`, while Raylib is a private implementation dependency.

The application owns simulation, networking, snapshots, gameplay roles, and configuration. The
viewer consumes prepared presentation data and returns user intent plus aggregate timing and draw
statistics.

## Input and ownership

`RenderEntityView` contains non-owning identifier, position, heading, and hue spans. The caller must
keep every span valid for the complete `Viewer::draw()` call. Mismatched lengths reject the entity
batch, and entities with non-finite positions or headings are skipped during preparation.

Other frame inputs are also caller-owned. `SpatialDebugView`, producer-resolved debug primitives,
selected-entity details, camera poses, and `RunSetupView` remain valid for the draw call.
Applications prepare the read-only Setup view from effective configuration and fingerprints. They
also resolve presentation interpolation before calling `Viewer::draw()`.

## Rendering

Entities are divided into 32 persistent hue buckets and submitted with one instanced draw per
non-empty bucket. Capability-gated overlays can show world bounds, axes, spatial cells, observer
volumes, selection state, and producer-provided debug geometry.

`ViewerConfig::entity_mesh_path` can select an OBJ model loaded during construction. An empty or
failed path uses the procedural wedge. Both the fallback mesh and reference `boid.obj` use local
`+Z` as forward and `+Y` as up. The configured entity scale remains the final visual scale.

## Cameras and controls

The camera modes are Overview Orbit, Entity Follow, stationary observer, and an
application-provided game camera. Entity Follow uses a stable `EntityNetId`. The observer and game
camera poses remain application-owned, and a missing requested pose falls back to Overview Orbit.

| Action | Controls |
| --- | --- |
| Open Overview, Network, Entity, or Setup inspector | `F1`, `F2`, `F3`, `F4` |
| Choose a camera | `C` |
| Orbit, zoom, or reset Overview and Entity Follow | Right drag, wheel, `R` |
| Select, step through, or clear entities | Left click, `[`, `]`, `Backspace` |
| Rotate the stationary observer | Arrow keys |
| Pause, choose overlays, or show help | `P`, `M`, `H` |
| Save a screenshot or exit | `F12`, `Escape` |

Game-camera input is returned as semantic state in `ViewerResult` for the application to map to its
protocol. Server pause is local. A ready Client requests authoritative pause while continuing to
apply snapshots.

## Inspectors and diagnostics

`F1` presents runtime, world, spatial, interpolation, and viewer facts. `F2` presents available
connection and replication facts. `F3` presents selected-entity details and optional deeper
diagnostics. `F4` presents the caller-prepared read-only Setup.

Selection stores a stable ID rather than a frame-local index. Optional details can include motion,
neighbor, spatial-query, steering, and replication values. The selected entity has a
presentation-only trail bounded to 2,400 displayed positions. Changing selection clears it, and it
never affects simulation or replication.

Spatial cells and debug primitives are bounded non-owning views. Inspector sections and overlays
appear only when the producer supplies the corresponding capability and data.

## Lifetime and performance

Construction rolls back every acquired Raylib resource after a later failure. Normal destruction
releases resources in reverse ownership order. Instance buffers grow to the supplied population and
are reused. Inspector rows, debug labels, visible spatial cells, and trail history remain bounded.

API fields such as `viewer_cpu_time` report steady-clock elapsed viewer work and exclude
`EndDrawing` presentation wait. They are not process CPU counters. Optional Tracy instrumentation
emits aggregate zones and plots without per-entity or per-cell tracing.

The panel font is JetBrains Mono Nerd Font Regular, distributed under the SIL Open Font License 1.1
in `src/render/assets/JetBrainsMonoNerdFont-OFL.txt`.

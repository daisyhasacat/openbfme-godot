# Mac rendering performance

The native Mac build uses Release (`-O3`) by default. Keep the simulation's
strict floating-point options: fast-math and LTO are forbidden by
`engine/cmake/SimFp.cmake`. Generated `build.ninja` files are not configuration
sources.

## Instance upload reuse

`W3DInstancer::write_buffers` compares each packed 16-float instance record
bit for bit before submitting a MultiMesh buffer. Bone animation continues to
update the palette texture. If animation or camera sorting leaves the packed
records unchanged, the existing GPU buffer is reused. Visibility counts still
update, and buffer allocation/growth forces a submission. Changed transforms,
opacity, pivot fades, house colors, lighting and draw order still submit the
changed buffer. This is client-only and changes no simulation state or quality.

`get_stats().buffer_uploads` counts actual MultiMesh submissions in the last
update; `buffer_writes` retains its existing meaning (models processed).
The smoke test checks 101 animated soldiers: changed bone palettes, zero
instance uploads, and three uploads when opacity changes or is restored.
It also exercises worker-pool changes, fades and transparent sorting.

Validation on Apple Silicon: Release extension build and the pose-culling
regression pass. The full smoke suite reports three failures also reproduced
with the original instancer: EXHEALING/EXHEALING_L sort priorities, interleaved
depths at those priorities, and the live instancer's occupied-pad model count.
These remain unresolved; the full smoke suite is not green.

The upload reduction is verified headlessly, not an end-to-end FPS claim.
For windowed measurements, use the same map, camera, armies, resolution and
renderer with `--perf`; compare frame-time p95/p99 as well as average FPS.
The comparison adds CPU work, so measure CPU-bound scenes too. Metal/GPU timing
and long gameplay sessions still need windowed profiling.

## Avoid unchanged pose and instance submissions

The instancer now distinguishes bone motion from the pose channels used in
MultiMesh instance records. It caches visibility, pivot fading, and the pivot
origins used for transparent depth sorting. A changed pose only rebuilds the
instance records if those inputs changed. Camera/node movement, instance
transforms, tint, lighting, hidden subobjects and opacity still invalidate the
records through their existing paths. Identical transform, packed tint,
infantry-light and sway assignments no longer invalidate a model.

Palette blocks are compared byte for byte before copying. Re-evaluating the
same matrices no longer uploads the full palette texture. Growth still forces
an upload, and real bone changes continue to update it. The new
`get_stats().palette_uploads` counter reports these updates per `update_now()`;
initial texture creation during palette growth is separate.

`mesh_upload_test.gd` checks unchanged bind-pose requests (four evaluations,
zero model-buffer rebuilds or palette uploads), actual visual changes,
palette growth and byte-identical mesh fixtures. Two animated soldiers with
different animation phases are compared against forced instance-buffer
rebuilds, including their transparent sorting data. These checks pass with
native Metal 4 / Forward+ on Apple Silicon and headlessly; pose culling also
passes. This verifies avoided work and output preservation, not an FPS gain.

## Skirmish menu entry

Skirmish's map list now uses a scoped row-measurement cache. Previously every
map image insertion remeasured all earlier rows through Godot's multiline
font API. Within synchronous population, unchanged rows reuse their measured
height; changed cells invalidate their row, and column-width changes invalidate
all rows. Cumulative heights, selection and per-insertion scrolling still run
in their original order, including auto-scroll and purge skins. The cache ends
before the current map is selected and never spans a rendered frame.

The shell warms mounted maps' display labels while the main menu is idle,
with at most eight names and a one-millisecond scheduling budget per frame.
A single file lookup can exceed that budget; subsequent lookups wait for the
next frame. Warming no longer blocks shell boot. It uses the existing
shell-session map-string cache and localization rules, including installed
maps and absent `map.str` files. Opening Skirmish immediately still resolves
any remaining labels on demand. Warming pauses outside the main menu.

Optional tool scripts (the map editor, benchmark, update UI, and gameplay
helpers) and automation checks load when used instead of being parsed as
main-scene dependencies. Startup logs now distinguish object-world setup,
audio readiness and main-menu readiness. The loading artwork remains during
required archive verification and initialization; these checks are not skipped.

Temporary timing probes on the same Apple Silicon scripted menu transition
measured map-list population at 307 ms before the changes and 5.2 ms after
the original eager warming and row-measurement changes (map-name work 0.7 ms,
the rest mostly row population). This is
one headless before/after observation, not a whole-frame latency guarantee;
first-use textures and renderer work are separate costs. The probes were
removed from the final build.

The list-box regression compares every row's height, cumulative height and
contents, total height, scroll position and selection against the unoptimized
path for fixed, scroll-at-end and auto-scroll/purge lists. It pins 254 font
measurements for 64 rows and checks font/column invalidation. Retail Skirmish
and lobby tests cover the resulting map order and controls.

## Late lobby-animation stall: hidden 3D browser

A separate stall remained after the map-list changes. `skirmish_map_search.gd`
waited ten active lobby frames, then created the optional Explore maps browser
and synchronously prepared the selected map's terrain, object templates and
models while the browser was hidden. A windowed Metal trace recorded a
1,118 ms frame gap at that point, with 55 new surface pipelines.

The search overlay now creates the browser only when Explore maps is pressed.
Its existing `show_browser` path selects and builds the initial preview, and
subsequent opens reuse the browser. The automatic countdown and its unused
`prepare` entry point are removed. Ordinary lobby entry, search and map
selection no longer build this optional 3D scene. Opening Explore maps for the
first time still pays its preview-loading cost.

The repeated windowed transition had no hidden MAP PREVIEW build or matching
late frame gap; an initial 31 ms frame from the lobby's native animated 3D
background remained, separate from the removed late stall. This observation
is not a guarantee about all maps or renderer caches. Temporary native timing
probes were removed from the final build.

`godot/tests/skirmish_lobby_lazy_browser_test.gd` needs no retail data and
checks lobby idle/re-entry, native search, no hidden creation/preparation,
explicit browser opening and instance reuse. The retail
`skirmish_map_browser_test.gd` also passes, including preview construction and
map selection/install behavior.

## Always present Skirmish loading progress

The synchronous progress callback now calls `RenderingServer.force_draw` with
buffer swapping enabled. Previously it drew into the renderer without
presenting those updates to the window. Skirmish presents 0% before loading,
then its real progress milestones. Gameplay begins immediately when loading
returns: there is no extra completion update, frame wait or minimum-duration
timer. The bar is presented by the progress callback while loading is active.

The game world's original visibility is saved while loading and restored on
both success and failure. Its partially built terrain stays hidden behind the
loading artwork until the load returns.

Validation: four existing loading-screen tests (83 assertions), a headless
scripted Skirmish, and a windowed scripted Skirmish. A render-frame observer
confirmed progress from 0% through 100% while LoadScreen.apt was still on the
stack, before gameplay; the completed bar was also captured and inspected.

The console's **Main · game advance total** includes animation state updates,
snapshot interpolation, effects/audio and render synchronization, as well as
simulation when it runs on the main thread. It is not a worker-wait measurement.
**Main · actual worker wait** measures the delta of recorded blocking waits,
including script/input calls outside advance. Presentation events, drawable
animations, drawable transforms and client effects/audio have separate opt-in
scopes so a high total can be attributed without assuming a large army.

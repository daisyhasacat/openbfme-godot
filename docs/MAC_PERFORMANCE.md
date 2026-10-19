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

## Skirmish menu entry

Skirmish's map list now uses a scoped row-measurement cache. Previously every
map image insertion remeasured all earlier rows through Godot's multiline
font API. Within synchronous population, unchanged rows reuse their measured
height; changed cells invalidate their row, and column-width changes invalidate
all rows. Cumulative heights, selection and per-insertion scrolling still run
in their original order, including auto-scroll and purge skins. The cache ends
before the current map is selected and never spans a rendered frame.

The shell also resolves the mounted maps' display labels during boot, warming
its existing map-string cache before the main menu becomes interactive. This
moves first-use `map.str` reads (including probes for absent files) out of the
Skirmish click. It uses the mounted map list and existing localization rules,
including installed maps; it does not instantiate a hidden lobby or run its
scripts. The cache has the same shell-session lifetime as before.

Temporary timing probes on the same Apple Silicon scripted menu transition
measured map-list population at 307 ms before the changes and 5.2 ms after
both changes (map-name work 0.7 ms, the rest mostly row population). This is
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

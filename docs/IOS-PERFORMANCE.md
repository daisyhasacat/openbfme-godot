# iOS performance defaults

The Mobile renderer is already selected by the iOS project override. iOS display
settings now start at 60 FPS and 75% linear 3D resolution, including iPhone 15 Pro.
This renders 56.25% of the native 3D pixel count, reducing pixel shading and render
target bandwidth. HUD and menus retain their 2D resolution. Bilinear scaling
avoids an additional sharpening/upscaling pass; users may select FSR if desired.
These are presentation defaults, not simulation changes or measured FPS claims.

Existing saved frame caps and render scales override these defaults. A config
that explicitly stores 0 FPS cap / 100% resolution retains that choice; select
60 FPS and 75% in Display options to use this budget on an existing install.
The command-line frame cap still wins for a run. Reset to Defaults uses the iOS
values. Desktop defaults remain uncapped / 100%.

The object culling and cleanup changes in RENDER-CULLING.md also apply to iOS:
off-screen and undiscovered model instances are excluded from GPU draw buffers,
hidden poses are deferred, and fully sunk trees release their instances.

Further device validation is required for long battles, sustained frame time,
thermals, memory and image quality. The short physical-device measurements below
do not establish sustained performance.
The 60 FPS cap is a target, not a guarantee. No thermal-state adaptive controller
or automatic device-model detection is introduced.

Validation: iPhoneOS arm64 extension built successfully with the installed Xcode
SDK; IOS DISPLAY DEFAULTS PASS; existing OPTS1 settings/apply checks PASS. The
optional full-game OPTS1 subprocesses were skipped in this settings-only run.
The original defaults change had no physical-device measurement; the later
short benchmark below measures frame time, but not thermals.

## Physical iPhone startup investigation — 2026-10-21

A signed development build at `0dc438d8b6` was installed on the paired iPhone 15
Pro (iOS 27.0, build 24A5424a), retaining its downloaded game data and settings.
The Settings benchmark completed on the physical device with native Metal and
the Mobile renderer at 2556 × 1179, 100% render scale, UltraHigh detail and AA
off. The saved display configuration was uncapped with VSync on; the benchmark's
uncapped option temporarily disabled VSync. These settings override the newer
75%/60 FPS defaults. Instruments Time Profiler was attached during the run, so
these are diagnostic observations, not portable performance targets.

The real Men match startup measured:

| Measurement | Time |
| --- | ---: |
| Synchronous map load | 643.755 ms |
| HUD installation (`hud_started` to `hud_complete`) | 973.948 ms |
| Request to first submitted world frame | 1856.462 ms |
| Request to acknowledged gameplay input | 1886.172 ms |
| First submitted world frame to acknowledged input | 29.710 ms |
| Input probe queue latency | 0.029 ms |
| Longest frame gap in the post-reveal observation | 32.371 ms |

The first-world-frame marker is a render submission marker, not GPU completion
or visual recognition of a particular building. The native input acknowledgement
passes through the normal deferred HUD queue, with gameplay input enabled and no
modal; it is not just the PLAYING flag. The workload and measurement definitions
are in `SETTINGS_BENCHMARK.md`.

The four ten-second captures reported:

| Scene | Average FPS | Frame p95 | Frame p99 |
| --- | ---: | ---: | ---: |
| Main menu | 113.39 | 10.261 ms | 24.555 ms |
| Lobby | 82.89 | 29.284 ms | 35.672 ms |
| Men, 1,000 CP with zoom/pan | 54.14 | 32.754 ms | 34.029 ms |
| Men, 20,000 CP with zoom/pan | 28.38 | 58.099 ms | 82.979 ms |

### Confirmed earlier stalls and remaining uncertainty

The existing device session `openbfme-20261021-123344.log` at `971cd17897`
contains a main-thread freeze of 11.2 seconds immediately after Palantir appeared,
then a 7.2-second freeze during its first 3D model activity. An older session
(`openbfme-20261020-230540.log`) reports 12,478,950 microseconds inside the
Palantir canvas submission section. That section includes native 3D viewers and
Godot resource creation, so the CPU marker alone cannot distinguish asset
construction, shader compilation, driver waits or GPU work.

The current benchmark did **not** reproduce those multi-second post-base stalls.
The captured CPU profiles show canvas rendering, game/script work and some
shader/material creation; they do not establish the cause of the earlier freeze.
Some extension stack frames lack symbols. Cache state and first-use rendering
are plausible contributors, not proven causes. No fix for that freeze is claimed.
A useful next capture must catch the actual stall, with extension debug symbols
and a Metal/System Trace; do not delete the user's caches to force a reproduction.

A separate app-launch stall was reproduced after installing the update:
verification rehashed all 213 archives (zero cache hits) in 20,811 ms; initial
loading finished at engine elapsed time 23,008 ms. This occurred before the main
menu, not after the base appeared. The same kind of full rehash appears in the
previous session. The app-container path changes on updates, but why this
invalidated the verification cache has not been established here. Keep that
launch delay separate from map loading and gameplay-input readiness.

Local evidence is kept under `workspace/diagnostics/iphone-20261021/` (ignored):
the exported benchmark, session logs, two Instruments traces and their sampled
CPU stacks. No retail data or device credentials are committed. Validation:
iPhoneOS extension build, signed Xcode Debug build and on-device benchmark all
completed successfully. The app update preserves the phone's downloaded assets;
its saved display settings were restored by the benchmark.

## Shadow budget and pipeline diagnostics

Graphics → Shadow quality is an OpenBFME presentation option:

- **Auto** (default): Balanced on iOS; High on desktop.
- **Balanced**: two directional-shadow cascades, without cascade blending.
- **High**: four cascades with blending, matching the previous gameplay setup.

The original detail setting still disables shadows when its shadow LOD is off.
Both profiles keep the existing 3,000-unit coverage and shadow bias; reducing
coverage can remove shadows at wide zoom, so it is not reduced here. The same
profile helper configures actual matches and benchmark scenes. This also fixes
the benchmark's previous use of different cascade-blending/bias defaults from
the real match. Existing benchmark numbers therefore are not directly comparable
to the new captures. Profile changes preview immediately and Cancel restores the
previous profile. The chosen profile is stored as `OpenBFMEShadowProfile` in
Options.ini; an explicit High selection overrides Auto on iOS.

Godot documents four cascades as its slowest directional-shadow mode and notes
the added cost of split blending:
https://docs.godotengine.org/en/4.7/classes/class_directionallight3d.html
Two cascades trade near/far shadow detail and transition smoothness for less
rendering work; there is no measured iPhone FPS improvement claim yet.

The Settings benchmark records Godot's cumulative compilation counters at
request, map load, HUD setup, first world frame and observation completion.
Reports include deltas for the entire startup, the period after HUD setup, and
each timed FPS capture, for canvas, mesh, surface, draw and specialization
pipelines. The results screen shows canvas/draw counts after HUD setup. Counters
are counts, not milliseconds; compilation activity near a hitch does not prove
that it caused the hitch. Headless/Compatibility runs label these measurements
unavailable. Godot's pipeline guidance explains that 2D canvas pipelines still
compile on first draw, while many 3D pipelines can be prepared during loading:
https://docs.godotengine.org/en/4.7/tutorials/performance/pipeline_compilations.html

No forced HUD warmup is added without evidence that compilation is the remaining
cause. The new counters let the next physical-device reproduction distinguish
that case without deleting caches. Cached W3D shaders, instancing, frustum
culling and the existing 30 Hz iOS menu-decoration cadence remain in use.

Validation: `render_budget_test.gd` checks platform defaults, explicit overrides,
actual light properties, retail shadow-off behavior and invalid-setting reports.
`settings_benchmark_test.gd` checks profile metadata and pipeline milestones in
addition to its loading, input readiness and restoration checks. Its optional
`BENCHMARK_SHADOW_PROFILE` and `BENCHMARK_RENDER_SCALE` environment variables
select test configurations without saving them to the user's settings file.

Short Mac Metal comparison (three-second captures, same 3024 × 1898 output;
single runs, not sustained or repeated trials):

| Configuration | Main menu FPS | Lobby FPS | 1,000 CP FPS | 20,000 CP FPS |
| --- | ---: | ---: | ---: | ---: |
| Forward+, High shadows, 100% scale | 462.1 | 90.6 | 166.9 | 76.1 |
| Mobile, High shadows, 100% scale | 567.7 | 254.8 | 268.9 | 83.4 |
| Mobile, Balanced shadows, 75% scale | 615.2 | 224.6 | 192.5 | 82.3 |

The reduced-budget row changes two variables and was not consistently faster.
These captures do not isolate a shadow benefit or establish thermally sustained
performance. The same-quality renderer comparison favors Mobile in this run,
but the Mac default remains Forward+ pending visual/feature validation (including
TAA/FSR users). Both native renderers passed the full benchmark orchestration,
input readiness, cancellation and restoration checks. On the Forward+ run,
post-HUD counts were three canvas, eight surface and 41 specialization pipelines,
with zero on-demand draw pipelines; this is compilation activity, not a measured
cause of a freeze. Raw reports are retained under
`workspace/diagnostics/shadow-budget-20261021/` (ignored).

## iPhone Duo simulator, 2026-10-21

The arm64 iOS 27.1 Duo simulator boots the current debug export and accepts the
local diagnostic status, screenshot and benchmark commands. The stock export
previously selected the wrong preset's custom template after preset reordering;
the exporter now identifies iOS by name and platform.

Menu freeze samples show Apple's `GLRendererFloat` / `glvmInterpretFPTransform`
software rasterizer below GLES canvas and embedded 3D scene draws. A native-size
menu frame stalled for 23.6 seconds; lowering the root viewport alone still left
15–27 second lobby frames. Godot's matching iOS platform build explicitly disables
Metal and Vulkan for Simulator. These are simulator functional tests, not iPhone
GPU performance measurements. Simulator exports now use a 960-wide viewport and
cap embedded APT 3D scene textures to 320 pixels on their longest axis.
The capped build renders the lobby and accepts screenshots and cancellation,
but still recorded a 16.5-second frame; these caps do not solve software rendering.

GLES also converted the shared CPU animation palette from RGBAF to RGBAH in place,
invalidating subsequent float32 writes and texture updates. Compatibility uploads
now use a separate image; iOS converts that upload to half-float explicitly.
The CPU palette remains float32 and native Metal retains its direct upload path.
Palette growth, pose preservation and animated buffer tests pass headless and
with the graphical macOS Compatibility renderer. The simulator benchmark was
cancelled in the lobby while investigating software-rendering stalls; no complete
20,000-CP simulator result is claimed.
The macOS native build and arm64 Xcode simulator build passed, as did all 11
menu/end-screen tests (214 assertions), three exporter regression tests, and the
commit gate. No palette-format update errors appeared in the final simulator log.

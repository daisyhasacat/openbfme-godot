# Settings benchmark

Open Settings → Benchmark → Open benchmark from the main menu. Apply any
settings changes first. The tool measures the current saved graphics settings;
its optional uncapped mode temporarily disables VSync and Engine.max_fps, then
restores both after completion, cancellation, or failure.

Each stage warms up for three seconds and captures ten seconds by default
(adjustable from 3–60 seconds):

1. Main menu, including its animated background.
2. Skirmish lobby.
3. A 12-building Men base and exactly 1,000 CP of mixed Men battalions and siege.
4. A fresh copy of the map with the same base and exactly 20,000 CP.

Between the lobby and army captures, a real Men skirmish start measures the
normal match-loading path, including audio, Palantir, the native HUD and mobile
controls. Results show synchronous map-loading time, the delay from the first
world frame to responsive controls, and the longest frame gap during the first
second after reveal. JSON includes prepare/load/HUD/end-screen-preload/PLAYING
milestones and the engine's detailed load timings.

The first world frame means `frame_post_draw` with the loaded world visible and
the gameplay camera current: render submission, not GPU completion or pixel
recognition of a building. Controls are ready when a harmless input probe sent
through Viewport input is acknowledged by the native HUD's ordinary input queue
after PLAYING, with input enabled and no modal. A busy worker therefore delays
acknowledgement. The probe posts no simulation commands. Headless runs report
render/input measurements unavailable. These are warm-process measurements,
not cold application launches; caches may already contain map assets. Run on the
iPhone itself to measure its delays. The tool does not create a replay, score or
match record for the temporary skirmish.

The preset is `godot/benchmarks/men.json`. Template names are scenario content,
not engine assumptions. Command-point costs come from the mounted templates;
the deterministic planner refuses an exact total that those costs cannot fill.
The army includes swordsmen, archers, Tower Guards, Rohirrim and trebuchets.
Real player CP usage is checked after creation, warmup and capture. Missing
content, script errors or a changed population fail the stage explicitly.

This is an idle synthetic workload, not a combat benchmark. Auto-acquisition
is disabled and units are invulnerable to keep the population stable. Units
and buildings occupy separate reserved areas. The normal and stress scenes
warm up at maximum zoom-out, then pan diagonally across the base and army while
zooming to minimum height. The first and last 20% of capture hold the endpoints;
the middle 60% moves continuously. Limits come from the live map camera in free
camera mode. Each report records the limits, endpoint camera states and a timed
position/height trace. Loading, spawning and warmup are excluded from the FPS captures; startup timings are
reported separately. The in-game scenes use a local viewer match and HUD, with the normal logic-thread preference. No score screen,
replay, game record or online result is created.

Results show average FPS, nearest-rank frame-time p95/p99 and average GPU
render time when available. Local JSON files in `user://benchmarks/` include
raw frame samples, nested CPU scope timings, workload composition, renderer,
GPU, display settings and camera state. `latest-settings.json` is shown when
the tool is reopened. GPU measurements can lag; missing measurements are
reported as unavailable. CPU scopes overlap and must not be added together.

Cancel using the visible button, Escape or controller B. Losing application
focus cancels the run so background throttling is not mistaken for performance.
The temporary world is cleared and the main menu and display caps restored.
Launching inside a match or lobby is refused.

Validation:

- `godot/tests/benchmark_plan_test.gd`: exact/deterministic CP planning,
  unfillable targets, invalid costs, camera path endpoints and frame statistics.
- `godot/tests/settings_benchmark_test.gd`: real Settings-tab launch, early
  real-startup and loaded-world cancellation, four stages, held-input
  acknowledgement, input isolation, exact CP, rendered zoom/pan endpoints,
  report serialization, main-menu restoration and cap restoration. Needs installed retail data.
  `BENCHMARK_FULL=1` uses three-second warmups/captures; otherwise captures are
  shortened for orchestration checks. Graphical execution validates rendering;
  headless execution only validates orchestration, never GPU performance.

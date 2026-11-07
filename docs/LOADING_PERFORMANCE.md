# Loading performance

## Background startup

Before the main menu, archive mounting/verification and cosmetic-pack indexing
run through `scripts/release/startup_worker.gd`. The main thread awaits frames
while the worker has exclusive access to the filesystem being prepared. Folder
selection, language warnings and error dialogs still run on the main thread.
The task joins before its owner leaves the tree, including during shutdown.

`GameWorld.begin_setup()` prepares the initial object world, map-object
options, map cache, logic settings and particle/FX definitions on a native
worker. `GameAudio.begin_boot()` parses audio, EVA and large-group audio data
on a separate worker; normal game startup overlaps these two tasks. Each task
owns its result and keeps its archive owner alive. `is_setup_ready()` and
`is_boot_ready()` poll without waiting; the existing `setup()` and `boot()`
methods publish completed results on the main thread. Calling them with a
different filesystem, or before completion, reports an error without consuming
the task. Repeated starts are refused. The original synchronous APIs remain
available to viewers and tests.

The object-world worker normalizes its floating-point environment and removes
its thread-local parser context before publishing. The main thread activates
that world only after joining. Both native task owners join pending work before
destruction, so archives and parser stores cannot be freed underneath a worker.
Load failures and exceptions are returned as startup errors.

Godot nodes, fonts, materials, textures, meshes and audio devices are still
created on the main thread. Lua-dependent scene loading also stays there:
lua-gdextension's resource loader uses its main-thread language state. Map-name
cache warming remains a small main-thread idle batch because menu lookups share
that cache. Background work keeps startup frames responsive; it does not remove
the need to finish validation before entering the menu.

`godot/tests/background_startup_test.gd` checks advancing frames during archive
mounting, synchronous/background setup report equality, audio event-name
equality, exact game-state hashes over five logic frames, mismatched finish
requests, overlapping-start refusal and destruction during pending loads.
`first_run_optional_rotwk_test.gd` checks both first-run mount rejection routes.
These checks passed on the local mounted data; a complete startup run reached
the main menu with audio and object parsing overlapped. This is functional
concurrency validation, not a whole-engine ThreadSanitizer result.

## Multicore terrain preparation

Terrain loading uses the existing client job pool (`JobSystem::client()`).
Its default is the available hardware thread count; set
`OPENBFME_CLIENT_THREADS=1` to compare with sequential execution or choose
another positive count to limit CPU use.

- Terrain texture classes read and decode independently. Each owns its images
  and diagnostics. Reports and read exceptions are consumed in class order;
  tile-presence bits and atlas packing remain sequential. Atlas packing uses
  per-call scratch storage rather than a shared static grid.
- Terrain normals and static lighting run in independent ranges of 1,024
  vertices. Small grids stay inline.
- Mesh construction runs per terrain chunk, with stable row-major output
  slots and private counters reduced after the workers finish. Base-layer
  arrays reserve their exact capacity before emission.

These changes prepare presentation data only. Simulation order, RNG, UV
formulas, triangulation and lighting formulas are unchanged. Godot nodes,
materials and GPU resources are still created on the calling thread. Mounted
files and terrain inputs must remain immutable while loading, as with the
existing parallel INI read-ahead.

## Measurements

Release build, Apple silicon Mac, 14 hardware threads, October 29, 2026.
The opt-in synthetic benchmark uses a 257-by-257 height grid and 48 terrain
classes, each with a 256-by-256 base texture and normal map in a memory-backed
archive. Values are medians of seven runs after one warm-up; worker creation
is excluded.

| Implementation | Client threads | Atlas preparation | Mesh preparation |
| --- | ---: | ---: | ---: |
| Original | 14 | 9.54 ms | 6.60 ms |
| Parallel loading | 1 | 10.74 ms | 7.08 ms |
| Parallel loading | 4 | 5.35 ms | 1.92 ms |
| Parallel loading | 14 | 5.32 ms | 1.14 ms |

The original sources were compiled with the same Release flags and linked
ahead of the core library into a separate test executable, using the same
benchmark object. These are stage measurements, not whole-application startup
speedups. One-thread execution has some scheduling/bookkeeping overhead.

Run the benchmark after building `openbfme_tests`:

```sh
engine/build/openbfme_tests --no-skip --test-case='terrain loading benchmark*'
```

The existing `godot/tests/match_load_performance_test.gd` also passed on the
verified mounted retail data: a new Buckland skirmish and two exact snapshot
restores. With the parallel implementation, its mesh-array stage measured
5–7 ms with the default pool and 27–30 ms with one client thread. The test
checks restored frame/state hashes and compares gameplay-map terrain with
the standalone viewer. These runs are headless and do not measure visible
first-frame latency or GPU pipeline compilation.

```sh
mkdir -p /tmp/openbfme-load-perf
LOAD_PERF_DIR=/tmp/openbfme-load-perf "$GODOT" --headless --path godot \
  --script res://tests/match_load_performance_test.gd
```

Remaining costs include Godot resource construction and reloading INI stores
when the previous game ends. The latter took about one second with the default
pool in this run. It clears map-specific overrides before the next game;
skipping it would violate world reset correctness. Existing parallel archive
hashing, INI read-ahead and AI layout loading remain in use.

## Correctness checks

`test_map_terrain.cpp` compares every emitted mesh array, chunk bounds and
counter across one, two and four client threads, with and without lighting.
Atlas checks cover exact pixels, tile locations and ordered missing/invalid
texture diagnostics. `test_perf2_loading.cpp` also checks ordered read
exceptions without losing earlier diagnostics.

The terrain tests and job-system tests pass with both one and four initial
client threads. A focused ThreadSanitizer executable, instrumenting the
terrain atlas, mesh builder, job system and terrain tests, passed without a
race report. This is not a whole-engine sanitizer run.

The broader Godot smoke test did not validate successfully on this checkout:
its archive-count assertion expected 331 archives but the mount reported
213, then `smoke_checks.gd:72` accessed a missing `mod-archive` key. The
script printed `SMOKE PASS` after that script error; that message is not
counted as a successful smoke result. The dedicated map/save test above
completed its assertions successfully.

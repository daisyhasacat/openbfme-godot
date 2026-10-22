# Match loading performance

## Same-session quickload

F9 now uses a retained-scene rewind when its save was created by the still-live
skirmish. `GameWorld.quick_load_game(path)` restores the snapshot into the current
game, retaining the map, terrain, scene and loaded assets. Repeated Buckland
quickloads measured 23–25 ms wall time on macOS, including save-file validation.
This is a local measurement, not an iPhone timing claim.

The fast path recognizes SHA-256 fingerprints of complete saves successfully
written by this match. The registry is discarded when the scene is cleared;
matching only a map name or filename never qualifies. Campaigns, log-only saves,
and saves from another session retain normal loading. A refused fast restore is
reported without retrying through a destructive full load. The existing
transactional snapshot loader checks the saved frame/hash and restores the
running state on refusal (or explicitly closes it if rollback itself fails).

The replacement recording is prepared before the rewind commits. The restored
branch continues in memory and remains saveable; the original on-disk replay is
closed at its original frame/hash. Rewind discards queued simulation/HUD input,
deferred movie presses, held controls, pending effects and end-game requests.
The camera and HUD remain present, with selection mirrored from the saved state.

`same_session_quick_load_test.gd` exercises repeated threaded and non-threaded
rewinds, newly created/killed objects, exact restored and subsequent frame hashes,
retained scene children, continued saves, malformed-file refusal and loss of fast
path eligibility after a normal reload. `quick_load_game_test.gd` covers the real
F9 flow and queued HUD input. Snapshot regressions passed 10 cases; the broader
closed-set probe has two existing failures (`fortification target id` and
`recovering object id` accept its injected values), reproduced with the previous
`LiveGame::loadSnapshot` code. The compiler/link/classification audit passed;
the AST audit remains unavailable without libclang bindings.

## Shared map parsing

The gameplay loader parses a map before constructing its terrain. Terrain now
reuses that validated map instead of reading, decompressing and parsing it again.
The terrain atlas receives a private height-map copy because packing texture
class offsets modifies it. Standalone viewers and editor requests still parse
independently. No simulation rules or save formats change.

## Measurements

Headless macOS runs using the local RotWK/BFME2 data measured the redundant
Buckland terrain map parse at 12–17 ms before this change. The reused-map stage
takes approximately 0.001 ms; this excludes the private height-map copy and
subsequent terrain construction. Whole-load times vary with rendering setup,
so these measurements do not establish a corresponding whole-load speedup.

The larger remaining saved-game delay is previous-game teardown: approximately
1.1–1.5 seconds in Buckland/Evendim runs. This includes the retail object-world
reset that reloads INI stores between matches. Snapshot restoration itself took
approximately 20–34 ms. The reset must preserve isolation between matches;
skipping it requires separate correctness work. Load reports now expose
`previous_game_teardown_ms` and include it in their stage total.

## Reset parser optimization

CPU sampling of repeated save loads found repeated model-condition list scans
and temporary string construction in the INI index/lookup comparisons. The
comparisons now fold ASCII directly, and the immutable 591-name model-condition
table has an index. Mutable INI tables still scan in order. The index owns its
keys and retains no pointers into a gameplay world. First-match behavior,
unknown-name errors, macro processing and condition bit numbers are preserved.

A subsequent paired macOS Buckland control/optimized run measured previous-game
teardown at 898/935 ms versus 833/856 ms (about 8% less on average). These are
two restores per build, not a cross-device performance guarantee; other runs
varied substantially. Total load times also varied with terrain construction.
The full INI-world rebuild still runs, so this reduces its parser overhead but
does not eliminate the remaining roughly 0.8-second reset in that run.

Validation passed all 123 retail map override sets and synthetic block overrides,
fresh-world fingerprints, subsequent-game/replay/peer hashes, exhaustive
model-condition names and mixed-case lookups, scanner edge cases, and two exact
saved-game restores. Compiler-flag, link and source-classification audits passed;
the simulation AST audit was skipped because its libclang bindings were absent.

## Reproducing

Set `ROTWK_INSTALL` and `BFME2_INSTALL` to the retail data directories, and
`LOAD_PERF_DIR` to a temporary writable directory. Run Godot with:

```sh
Godot --headless --path godot --script res://tests/match_load_performance_test.gd
```

`LOAD_PERF_MAP` optionally selects a map name (default `map wor buckland`). The
test compares reused and independently parsed terrain mesh/atlas reports,
checks that rendering preserves the gameplay state hash, then saves and restores
twice with exact frame/hash checks. It prints timings without machine-dependent
performance assertions. Buckland and Evendim passed, as did the terrain lighting
and map editor regressions. BFME1 data was unavailable for this validation.

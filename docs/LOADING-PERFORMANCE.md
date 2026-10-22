# Match loading performance

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

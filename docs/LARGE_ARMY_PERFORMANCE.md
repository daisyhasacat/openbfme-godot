# Large owned-army performance

Target acquisition used to gather every nearby attackable object, compute its
radius-query distance, and then reject friendly objects. A dense owned army
therefore repeated friendly-unit work for every soldier looking for an enemy.

`TargetFinder` now records adjacent runs of the same team within each spatial
cell. Enemy scans reject non-enemy runs before visiting their individual objects.
Enemy candidates retain their original cell/list order, distance calculations,
weapon checks and object-ID tie breaks. The general-purpose radius query still
returns friendly and hostile objects in its original order.

Team membership changes invalidate the summaries for every finder in the world,
including standalone finders. The refresh reclassifies the existing cell entries;
it does not move objects to different cells midway through a frame. Diplomacy is
read at query time, so team and player relationship changes apply immediately.
The membership version and summaries are derived caches, outside saves and state
hashes. This optimization preserves the existing acquisition behavior; it does
not resolve the retail targeting differences registered under S-326.

## Reproduce

Build the test runner, then run:

```sh
engine/build/openbfme_tests --test-case='large army targets*' --no-skip --no-colors
```

The synthetic benchmark creates 600 or 1,200 soldiers of five CP each, owned by
one player, with either no enemy or one enemy in acquisition range. Each sample
performs ten acquisition passes across the whole army, rebuilding the index at
the start of each pass. It measures thread CPU time, excluding world creation
and state hashing. It runs the original per-object scan and the optimized scan
in legacy/optimized/optimized/legacy order on the same world. Timing is reported,
not used as a pass threshold. The reference uses the unchanged public radius
query followed by the original relationship, range and weapon tests.

A local Apple Silicon Release build measured these averages of two samples:

| Owned CP | Soldiers | Enemy present | Original scan | Optimized scan | Speedup |
|---|---:|---|---:|---:|---:|
| 3,000 | 600 | No | 40.69 ms | 1.56 ms | 26.0x |
| 3,000 | 600 | Yes | 38.92 ms | 2.80 ms | 13.9x |
| 6,000 | 1,200 | No | 140.52 ms | 3.44 ms | 40.9x |
| 6,000 | 1,200 | Yes | 148.29 ms | 6.01 ms | 24.7x |

The target digests and complete world state hashes matched both paths in every
sample. Before changing production code, the benchmark also ran against the
original implementation: its four digests and world hashes match the optimized
build. These are target-acquisition measurements, not whole-game FPS or retail
match results. Mixed hostile armies still require visiting hostile candidates;
rendering, collisions, pathfinding and other simulation work are outside this
measurement.

## Validation

The five large-army tests (including the opt-in benchmark) pass 2,808 assertions.
They compare target choices with the original scan across acquisition flags and
cover same-frame diplomacy, seeker/victim team changes, null teams, standalone
finders, frozen cell assignments, creation, containment, destruction and reset.
An additional 89 combat, object-filter, fortification and formation regression
tests pass 2,542 assertions. The macOS GDExtension and test runner both build.

The simulation audit's compiler-flag and link checks pass. The full audit remains
blocked on this checkout: libclang Python bindings are unavailable (S-230), and
existing `src/GameClient/FloatingText.cpp` is unclassified by the simulation
manifest/exclusion policy. Neither audit policy nor its baseline was changed.

## Moving 20,000 CP armies

The collision phase previously rediscovered crate modules by walking every
behavior of every object on each of the six engine ticks. Even an army with
no crates paid for those RTTI checks. Every actual contact also walked the
whole behavior list to find its collision callbacks.

Objects now index their collision interfaces and crate classification once,
after building their immutable module list. The index includes helpers and
retains module order. Contact dispatch still stops immediately when the
object is destroyed. Module state, transfers and callbacks are unchanged;
the index is derived data outside state hashes and saves. Crate discovery
still visits live objects and retains its object-ID order, so creation,
destruction and restored objects need no separate registry to invalidate.

The collision grid now applies its existing float32 overlap test before
sorting neighbors. Only actual overlapping partners need sorting. The same
pairs reach the same callbacks in the same ID order. This reduces scratch
traffic and sorting in a large mixed-radius army; it does not change grid
size, pathfinding budgets, tick rate, collisions or movement rules.

### Reproduce the movement measurement

With the verified retail install paths configured, build `openbfme_tests` and
run this opt-in benchmark separately on the original and optimized binaries:

```sh
PATHPERF1_CASES=334,i5000 PATHPERF1_FRAMES=180 \
PATHPERF1_HASHES=/tmp/army-movement \
engine/build/openbfme_tests --test-case='pathperf1 bench*' --no-skip --no-colors
```

The existing Brown Lands march scenario settles for 30 frames, then sends one
selection/move message and measures 180 complete `LiveGame::advance` calls.
The first order frame is reported separately from the 179-frame march. Each
run reports actual army CP: 334 Gondor fighter battalions use 20,040 CP;
5,000 individual Gondor fighters use 20,000 CP. Creation, map loading and
state hashing are outside the timed interval. These are thread CPU times
for the native live-game simulation/client path, not rendered FPS or a GPU
benchmark. Explicitly mounted mods can have different CP costs and workloads.

`PATHPERF1_HASHES` writes every measured frame's state hash to a separate file
per case; compare those files between builds. Hash output is optional and does
not affect simulation state. The benchmark itself is never a timing threshold.

### Measured result

On an Apple Silicon Release build, original/optimized/optimized/original runs
with no concurrent builds/tests produced these averages of two samples:

| Army | CP | Original march mean | Optimized march mean | Less CPU time |
|---|---:|---:|---:|---:|
| 334 battalions | 20,040 | 55.05 ms | 40.94 ms | 25.6% |
| 5,000 individual soldiers | 20,000 | 29.83 ms | 20.34 ms | 31.8% |

Every one of the 180 state hashes per scenario matched across all four runs.
The battalion hash chain was `1dc34080`; the individual-soldier chain was
`7878afd5`. Path queue peaks were also unchanged (339 and 510 respectively).
The order-frame cost remains significant, particularly for 5,000 individual
orders; this change does not claim to eliminate that spike.

Both the macOS extension and native test runner build. The focused movement,
pathfinder and crate suite passes 30 cases / 1,399 assertions, including
cached-interface order, restored crate state, pickup once-only behavior,
containment/destruction and repeated movement determinism. The opt-in
two-scenario benchmark passes 5,772 assertions per run. Compiler/link audit
checks pass; the full AST audit remains blocked by the existing missing
libclang and unclassified FloatingText source described above.

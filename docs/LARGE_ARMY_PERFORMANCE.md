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

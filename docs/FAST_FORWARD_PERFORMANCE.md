# Fast-forward performance

The seven-native-AI benchmark uses Fall Back 8p, one human, seven Hard AIs,
seed 4242, five game minutes and 100x time scale:

```
godot --headless --path godot --script tests/ai_fast_forward_bench.gd
```

Set `ROTWK_INSTALL` and `BFME2_INSTALL` to the installed retail folders. The
benchmark reports elapsed time, completed simulation frames, object count and
state hash. It runs logic synchronously to measure throughput without a worker
backlog. It includes client synchronization with the headless renderer, but does
not measure an interactive window's frame rate. Timings are observations, not
pass/fail thresholds; compare hashes and frame counts before comparing speed.

CPU sampling of this setup found repeated KindOf name resolution in script and
AI filters. The immutable lookup now hashes ASCII-folded string views instead of
binary-searching folded names. Keys still come from the same retail table,
duplicate names retain the first index, and unknown names return -1. This changes
neither AI frequency nor simulation rate, ordering, randomness or arithmetic.

On the development Apple Silicon machine, the existing five-minute AI probe
improved from 20.2 seconds to 13.1 seconds. Its complete per-minute reports
matched. The explicit 100x benchmark measured 23.33 seconds before and 13.15
seconds after, with 1500 frames, 1011 objects and hash 3913244160 in both builds.
These are local single-run measurements subject to machine load; they do not
establish that every late-game match can sustain 100x.

`test_perf1_bench.cpp` compares lookup results to the original linear scan for
all retail names, mixed casing, missing names and non-terminated string views.

Validation: the lookup test passes 1344 assertions. The broader PERF-1 suite
passes 14043 of 14045 assertions on this installation: both stored historical
hash vectors differ at frame zero. The original binary-search lookup reproduces
those same two failures; the reference files were not regenerated. The seeded
seven-AI before/after benchmark above provides a current-build hash comparison.

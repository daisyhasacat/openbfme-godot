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

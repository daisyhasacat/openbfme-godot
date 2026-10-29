# BFME2 shell scene rendering

The main menu previously rendered only the shell W3D model through the generic
APT View3D path. Its 4:3 render target was stretched to the window, and it used
the viewer's lights instead of the shell map environment. Water and particle
proxies were absent. The separate black-background regression is documented in
`IOS-MENU-PERFORMANCE.md`.

The shell scene now reads GameData `ShellMapName` from the mounted BFME2
presentation archives, loads the map's GlobalLighting and Weather, and resolves
the system actor's default model and animation. Model placement includes
terrain-relative Z. Shell scripts drive the authored named cameras; ordinary APT
View3D decorations retain their CAMERA bone path. The scene renders at the actual
window aspect while the APT menu retains its own stage mapping.

Standing water uses the existing map water loader and shader, with an additional
scene reflection camera at the authored `ReflectionPlaneZ`. Water is excluded
from its own reflection pass. Particle proxies load their map.ini default
`ParticleSysBone None` systems. Unsupported named bone bindings fail explicitly.
Scene lights and fog belong to these materials; gameplay shader globals and
simulation RNG are unchanged. Reflections inherit the parent render scale and
View3D update budget and stop rendering when the backdrop is hidden.

Evidence from Open-BFME-2 (BFME2 source, not a claim about RotWK differences):

- `Code/GameEngine/Source/GameClient/GUI/Shell/Shell.cpp`,
  `Shell::showShellMap` at 0x35C194: starts the GameData shell map in game mode 4,
  seeds its game random stream with zero. This presentation path loads the
  visual scene with a separate FX random stream, without starting a shell game.
- `ScreenHilightFilterPostRender.cpp` and `W3DShaderManagerPerformGlow.cpp`:
  emissive-only target, blur, additive composite. This remains S-5050; substituting
  full-scene bloom would not reproduce that behavior.
- Runtime content supplies the model, animation, fog, lighting, water plane and
  proxy system names. No extracted assets or reference screenshots are committed.

Camera fades now run the map's timer/counter/enable script graph in an isolated
presentation context using the existing ScriptEngine. `CAMERA_FADE_ADD` follows
Open-BFME-2 `ScriptEngine.cpp` setFade (0x203777) / updateFades (0x2036CC):
updates before script evaluation, an immediate update when increase is zero,
inclusive phase boundaries, and decrease divided by `framesDecrease + 1`.
The additive white amount is applied when composing the backdrop, before menu
controls. Render-only interpolation samples between adjacent fade values at
display cadence, independently of the 3D decoration budget. The 5 Hz script
values and timer edges remain unchanged. This is a presentation smoothing choice,
not a claim that BFME2's exact render interpolation has been recovered.
Timing catches up all 5 Hz script ticks after a render stall. No cut
times, script names or fade durations are hard-coded. Unsupported effect script
conditions/actions or scheduling fail with their names. This context supports
the timing and named-camera graph only; UI/capture actions do not change the
active game or menu.

Named camera moves now resolve the map's markers, interpolate position, shortest
yaw arc, zoom, FOV, roll and look-at height over the authored duration/easing, and
switch start markers while the white fade covers the cut. The installed map has
four start/end pairs; names, counts, times and locations are runtime data.
Evidence from Open-BFME-2:

- `ScriptActionsRva003BACCA.cpp` 0x3BAAE4: camera-marker lookup overrides a
  waypoint position, then passes the complete marker to the tactical view.
- `W3DViewRva0008A2EF.cpp` 0x8A2EF and `Rva00086B2CArrayInitialize.cpp`
  0x86B2C: named camera fields, two-point path, scalar moves, averaging of one.
- `W3DViewMoveAlongWaypointPath.cpp` 0x86F56: eased distance, ground interpolation
  and shortest yaw arc. Its extrapolated control points reduce a two-marker path
  to a line; no custom camera curve is invented here.
- `W3DCamTransform.cpp` 0x102F83 and `Rva00BCF670CameraSettings.cpp`: the map
  camera offset, zoom, ground plane and look-at transform. The serialized Pitch
  reaches View+0x70 through slot 61 (0x88FF6), which is descriptor `fxPitch`;
  treating it as the ordinary View+0x2C pitch rotation points into the cliffs.

The supported shell script set never enables live/real-zoom cameras or changes
that ordinary pitch rotation. General camera modes remain outside this loader.
Missing named markers fail with the requested name.

Scope: the current loader resolves the single system-model actor and unbound
particle proxies used by the installed BFME2 shell. It is not a full shell-game
implementation: general shell scripts, ambient audio, arbitrary object inheritance and
additional drawable actors need the full map object path. Existing W3D shader
and water acceptance stops also apply, including S-026 BUMPENVMAP. Exact visual
parity remains unclaimed.

Validation: native water/weather/lighting/shader tests, graphical
`shell_backdrop_test.gd` (scene fields, ground-relative camera, particle emission,
scene-owned uniforms, animation and hide/restore), and
`menu_viewport_budget_test.gd` (render scale, scheduling and cleanup).
Native `shell effects:*` tests pin fade endpoints, timer edges, stalled-clock
catch-up and explicit rejection. The graphical shell test also waits for the
first camera transition, verifies a white backdrop pixel and fade recovery.
`shell_fade_smoothing_test.gd` checks that the rendered fade value changes between
script ticks without spending a 3D update under a 1 Hz decoration budget. A rendered
main-menu frame is inspected locally; capture and retail data stay outside Git.

The retail camera-cycle test loads the installed BFME2 map and checks all four
segments plus restart. Synthetic camera tests pin field interpolation, the
look-at height calculation and missing-marker errors. The graphical test checks
that the first white cut advances to the second camera pair; local rendered
frames are visually inspected.

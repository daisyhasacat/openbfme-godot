# BFME2 shell scene rendering

The main menu previously rendered only the shell W3D model through the generic
APT View3D path. Its 4:3 render target was stretched to the window, and it used
the viewer's lights instead of the shell map environment. Water and particle
proxies were absent. The separate black-background regression is documented in
`IOS-MENU-PERFORMANCE.md`.

The shell scene now reads GameData `ShellMapName` from the mounted BFME2
presentation archives, loads the map's GlobalLighting and Weather, and resolves
the system actor's default model and animation. Both model and CAMERA bone use
the map placement, including terrain-relative Z. The scene renders at the actual
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

Scope: the current loader resolves the single system-model actor and unbound
particle proxies used by the installed BFME2 shell. It is not a full shell-game
implementation: shell scripts, ambient audio, arbitrary object inheritance and
additional drawable actors need the full map object path. Existing W3D shader
and water acceptance stops also apply, including S-026 BUMPENVMAP. Exact visual
parity remains unclaimed.

Validation: native water/weather/lighting/shader tests, graphical
`shell_backdrop_test.gd` (scene fields, ground-relative camera, particle emission,
scene-owned uniforms, animation and hide/restore), and
`menu_viewport_budget_test.gd` (render scale, scheduling and cleanup). A rendered
main-menu frame is inspected locally; capture and retail data stay outside Git.

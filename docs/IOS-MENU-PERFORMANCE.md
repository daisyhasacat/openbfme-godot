# iOS menu rendering audit

The Apt 3D decoration renderer creates two SubViewports for each model (black
and white clears reconstruct its blend over the UI). Previously both rendered
every display frame at their own full 3D scale, unaffected by the main window's
render-scale setting. Their W3DInstancer also ran its automatic update in
addition to the explicit menu pose/update path. These are verified sources of
unnecessary work; they are not a measured physical-device bottleneck ranking.

Changes:
- Disable automatic instancer updates and explicitly advance the mapper clock.
- Inherit the parent viewport's 3D render scale for both blend viewports.
- Schedule paired UPDATE_ONCE renders at 30 Hz on iOS, independently of menu
  input, the Apt clock and the application's 60 FPS cap. Animation uses elapsed
  real time. New frame commands, resize and visibility re-entry render promptly.
- Keep desktop cadence uncapped by default. Project setting
  openbfme/rendering/menu_3d_max_fps can override the decoration cap (0 uncapped).
- Report view3d_render_updates in AptMenuPlayer.get_report().

At the default 75% scale and 30 Hz decoration cap versus full scale at 60 Hz,
the paired 3D passes have a nominal 28.125% pixel-frame budget. This is not a
whole-menu FPS prediction: canvas composition, texture copies, UI submission
and asset/shader loading remain. No simulation behavior changes.

Validation: macOS extension and iPhoneOS ARM64 extension builds pass. The new
menu_viewport_budget_test.gd passes on the Mobile GPU renderer, checking paired
viewports, scale inheritance, disabled automatic updates and no continuous
viewport rendering. The existing menu_timing_test.gd covers menu transitions.
Physical iPhone frame times and thermals remain unmeasured; CoreDevice service
timed out during device discovery on this host.

Desktop shell-backdrop regression: a full canvas build must mark View3D slots
unused only once, before drawing the animated backdrop. A second beginBuild
between the backdrop and menu segments discarded the backdrop's used flag and
disabled its paired viewports, leaving a black main menu. The full build now
shares one View3D lifecycle across backdrop and menu segments; incremental
segment rebuilds retain their path-scoped lifecycle.

Verified with a rebuilt macOS extension and the graphical
`godot/tests/shell_backdrop_test.gd`: the prior build failed its animation-advance
assertion, while the fix passes animation, settings backdrop, hide and restore
checks. The rendered main-menu frame was visually checked. No simulation change.

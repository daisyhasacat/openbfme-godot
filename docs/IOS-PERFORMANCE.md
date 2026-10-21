# iOS performance defaults

The Mobile renderer is already selected by the iOS project override. iOS display
settings now start at 60 FPS and 75% linear 3D resolution, including iPhone 15 Pro.
This renders 56.25% of the native 3D pixel count, reducing pixel shading and render
target bandwidth. HUD and menus retain their 2D resolution. Bilinear scaling
avoids an additional sharpening/upscaling pass; users may select FSR if desired.
These are presentation defaults, not simulation changes or measured FPS claims.

Existing saved frame caps and render scales override these defaults. A config
that explicitly stores 0 FPS cap / 100% resolution retains that choice; select
60 FPS and 75% in Display options to use this budget on an existing install.
The command-line frame cap still wins for a run. Reset to Defaults uses the iOS
values. Desktop defaults remain uncapped / 100%.

The object culling and cleanup changes in RENDER-CULLING.md also apply to iOS:
off-screen and undiscovered model instances are excluded from GPU draw buffers,
hidden poses are deferred, and fully sunk trees release their instances.

Device validation still required: a long battle on iPhone 15 Pro with Metal
profiling, checking sustained frame time, thermals, memory and image quality.
The 60 FPS cap is a target, not a guarantee. No thermal-state adaptive controller
or automatic device-model detection is introduced.

Validation: iPhoneOS arm64 extension built successfully with the installed Xcode
SDK; IOS DISPLAY DEFAULTS PASS; existing OPTS1 settings/apply checks PASS. The
optional full-game OPTS1 subprocesses were skipped in this settings-only run.
No physical-device frame-time or thermal measurement has been made.

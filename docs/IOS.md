# iOS build

Requires a Mac with Xcode's iPhoneOS SDK, CMake, Ninja, Python 3, and the Godot
4.7 editor. The device target is ARM64, iOS 26 or newer. Build the macOS
extension first (the editor uses it while importing scripts), then:

```sh
tools/release/build_ios.sh
python3 tools/release/package_ios.py --output-dir engine/build-ios/export-main-sync
```

Open `engine/build-ios/export-main-sync/OpenBFME.xcodeproj` in Xcode and select
your development team and device for signing. Every Xcode build first rebuilds
the iOS framework from the main checkout and regenerates the Godot script/resource
pack. It updates only the PCK and extension framework in the export, preserving
Xcode signing settings. Changes to the generated project structure or native
plugins still require a fresh export.

`openbfme-source.json` beside the Xcode project records the source checkout and
Godot editor paths. Set `OPENBFME_SOURCE_DIR` or `GODOT` in the build environment
to override them when moving the export. The source checkout must remain on disk.
The refresh script adds standard Homebrew/CMake tool locations to Xcode's
minimal PATH, so Finder-launched Xcode can find CMake, Ninja and Python.
The build uses its current working-tree code; it does not pull, reset, or overwrite
your Git changes. Update the main checkout normally to build newer code.

Exports use external game data by default. The exporter ignores `ROTWK_INSTALL` and
`BFME2_INSTALL`, provides asset bundling through Xcode build settings, and excludes local
`mods` and `retail` folders from the staged Godot project. UI scripts and other
application resources remain in the PCK. Existing data-bearing exports are not
rewritten: use the new export and clean old build products before installing.

## External game data

On iPad, connect the drive and choose **Choose folder on drive**. In Apple's
Files picker select `OpenBFME` (containing `retail/rotwk` and `retail/bfme2`) or
`retail` itself. The app keeps data on the drive and saves a security-scoped
bookmark to restore access on later launches. Keep the drive connected while
playing. If access cannot be restored, reconnect and choose the folder again.

Exports enable `LSSupportsOpeningDocumentsInPlace` and `UIFileSharingEnabled`.
The native folder picker opens on first launch or when a bookmark cannot be
restored. Physical-device external access and gameplay still require validation.

A signed device build requires an
Apple development identity and provisioning profile. Pass `--team-id` or set
`APPLE_TEAM_ID`, or configure signing in Xcode after export. Existing export
projects are never overwritten; choose a new output directory to export again.

## Startup timing

The native launch screen and lightweight iOS startup scene show launch feedback
before the full game scene loads. `STARTUP` log lines record engine elapsed time
at the loading screen, game-scene loading, game readiness, archive verification
(including hashed/cached archive counts), and initial-load completion. Existing
world and shell logs report their initialization durations. A first mount may
read all game archives for integrity verification; repeat mounts use the MD5
cache. Data mounting remains synchronous, so its loading message can remain
static while the drive is being read. The startup screen shows a progress bar
using Godot's scene-loading progress for “Loading OpenBFME” and an indeterminate
bar for “Verifying game files”; verification has no incremental callback, so it
does not display a percentage. Its animation can pause during synchronous work.
Physical-device logs are needed to measure
the contribution of Metal/engine startup before the first script executes.

Game audio uses the iOS Playback session category, so the silent switch does not
mute the game. System volume and in-game volume controls still apply.

## Building completion performance

Menu transitions have separate CPU diagnostics. `AptMenuPlayer.get_stats()`
retains `peak_steps_us`, `peak_list_us`, `peak_canvas_us`, and `peak_submit_us`
until `reset_timing()` (or boot). These cover the movie/window update, render-list
generation, canvas construction including first texture decoding, and Godot draw
submission respectively. Stages totaling at least 50 ms print `MENU STALL` with
the current screen and stage durations. This reports CPU work; a GPU stall after
submission still needs a device trace. `godot/tests/menu_timing_test.gd` exercises
Main Menu, Options, and Skirmish transitions and verifies the peak counters.

Completed buildings and fortress upgrades can introduce a model for the first
time. Mesh creation now fills Godot vertex buffers through bulk writable storage
instead of making extension calls per vertex component. Adding a model also
binds only its new materials to the animation palette; existing materials are
rebound when the palette texture grows.

`godot/tests/mesh_upload_test.gd` checks byte-identical vertex/index buffers
against the previous renderer and verifies palette bindings before and after
growth. Its load timings are diagnostics, not a portable FPS threshold. These
changes reduce CPU work; they do not establish physical iPad shader compilation
or texture-upload latency. `GameWorld.get_frame_timings()` exposes `new_models`
and `new_model_ms` to distinguish synchronous model creation from other stalls.

## Touch and Apple Pencil controls

Native text fields use one explicit soft-keyboard presentation path. The hidden
Godot editor's automatic keyboard requests are disabled, and repeated taps do
not request another presentation while the keyboard is visible or during its
opening interval. Switching fields still updates the native editor's text.
Physical-device testing is required to measure UIKit's first-open delay and any
associated audio interruption.

Drag one finger over the battlefield to pan the camera. Pinch two fingers
together to zoom out, or spread them to zoom in; moving both fingers also pans.
A finger tap on a selectable unit or building replaces the current selection;
tapping empty ground issues the normal contextual order. Explicit ability and
building-placement targeting retain their normal behavior. Tapping a capturable
flag with an eligible unit selected starts capture and keeps that unit selected.
Apple Pencil keeps
mouse-style selection: tap to select, or drag to draw a selection box. Drags
start after 12 pixels of movement at a 720-pixel viewport height, scaling with
larger viewports, so small movements during a tap do not start a drag.

Swipe lists and scrollable panels to scroll, including the map list and native
menu lists. Tapping still activates their entries. Interrupted touches and
canceled Pencil strokes release capture without activating the control below.

The classic native HUD gains a top touch toolbar on touch devices: Deselect,
Stop, Menu, Console, Attack Move (A), Select Army (Q), Home Base (H), and Last
Event (Space). These shortcuts use the same input path as the keyboard. The
modern HUD retains its own controls.

The iOS bridge distinguishes Pencil from fingers before Godot emulates mouse
input. Automated gesture tests and simulator compilation cover routing and
selection behavior; physical Pencil interaction still needs device validation.

## iOS Simulator (Apple Silicon)

The export uses native arm64 simulators with a matching source-built Godot library. Its pre-build phase reads Xcode's selected
platform and rebuilds/copies the matching framework automatically. Select an
iPad Simulator in Xcode and build. Switching back to a physical iPad rebuilds
and copies the device framework; the two native build directories are separate.

To prepare the simulator framework explicitly:

```sh
tools/release/build_ios.sh --simulator
python3 tools/release/package_ios.py --simulator --output-dir engine/build-ios/export-simulator
```

Simulator builds go to `engine/build-ios-simulator` and
`godot/bin/ios-simulator`, leaving the physical-device framework intact. The
export references the same framework name; the source-refresh phase installs
the platform-appropriate binary before linking. The official Godot 4.7 template ships an x86_64-only simulator library. Build
its matching arm64 library (with SCons installed) from Godot commit `5b4e0cb0fd279832bbdd69fed5354d4e5ad26f88`
in `workspace/godot-ios-source`:

```sh
scons platform=ios target=template_debug arch=arm64 simulator=yes vulkan=no metal=no -j8
```

The exporter inserts it into a local template automatically and selects the
Compatibility renderer for the simulator. The project selects arm64 for simulator
builds; no Rosetta destination is required. Physical iPad builds remain arm64
with the Mobile renderer.
Simulator interaction cannot validate physical iPad performance, removable-drive
access, or the silent switch.

## Optional bundled game assets

In the **OpenBFME target → Build Settings → User-Defined**, set:

- `OPENBFME_BUNDLE_ASSETS`: `YES` to bundle, `NO` for external storage (default).
- `OPENBFME_ROTWK_ASSETS`: absolute path to the RotWK installation.
- `OPENBFME_BFME2_ASSETS`: absolute path to the BFME2 installation.

The final build phase copies the installations into the built app's `retail`
folder before signing, for either device or simulator. `rsync` updates changed
files on subsequent builds. Setting the toggle back to `NO` removes that folder
from the built app; original installations remain untouched. Allow several GB
of additional disk space and a longer initial build. Nothing is committed to Git.

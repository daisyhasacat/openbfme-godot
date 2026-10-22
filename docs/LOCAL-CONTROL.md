# Local diagnostic control

Development builds create `user://local-control/` after the game reaches its
menus. This local file protocol lets a paired developer computer control the
app without touch input. Release builds do not start it. Set
`OPENBFME_LOCAL_CONTROL=0` to disable it in a development build.

There is no network listener, arbitrary script/Lua execution, shell execution or
filesystem-path parameter. iOS access uses Apple's existing paired-device app
container access. The phone must be unlocked and the app must remain in the
foreground; a stalled main thread cannot process commands until it resumes.

Use the helper from the repository:

```sh
python3 tools/debug/local_control.py --device '<iPhone UDID or name>' status
python3 tools/debug/local_control.py --device '<iPhone UDID or name>' --timeout 360 \
  benchmark_start --seconds 3 --shadow-profile balanced --render-scale 0.75 --wait
python3 tools/debug/local_control.py --device '<iPhone UDID or name>' benchmark_cancel
python3 tools/debug/local_control.py --device '<iPhone UDID or name>' settings --tab Graphics
python3 tools/debug/local_control.py --device '<iPhone UDID or name>' screenshot
```

For a desktop development build, replace `--device ...` with `--directory` and
the absolute path to its `local-control` folder. The app prints that path at
startup. Menu navigation uses `menu_input --action up|down|left|right|accept|back`.
Camera movement uses `camera --x 2000 --y 1200 --height 1000` (SAGE map coordinates
and height above ground); it requires a live game outside the benchmark. Menu
input is refused during gameplay unless a menu is open. Gameplay orders, chat
and online session management are outside this protocol.

`benchmark_start` requires the main menu and uses the normal Settings benchmark.
Capture duration is 3–60 seconds. `--capped` retains the existing frame cap and
VSync. Optional shadow and 0.5–1.0 render-scale overrides are temporary and are
restored on completion, cancellation or controller shutdown. They are never
saved to Options.ini or the display config. Reports record the diagnostic
request and actual render scale/profile. Reaching the report does not establish
that the results are comparable to a different device, cache state or renderer.

## Files and acknowledgements

Write a JSON object to `request.json` with `version: 1`, a unique `id` containing
1–64 ASCII letters/digits/hyphens/underscores, and a `command`. For example:

```json
{"version":1,"id":"bench-001","command":"benchmark_start","seconds":3,"shadow_profile":"balanced","render_scale":0.75,"uncapped":true}
```

Commands are polled at 4 Hz. Input is limited to 32 KiB. An incomplete JSON copy
is allowed two seconds to finish. The app consumes the file, persists a receipt
before starting an action, and writes `response.json`. Reusing an accepted ID
returns `duplicate`, including across controller/app restarts and intervening
status requests. A running job is marked failed on restart; it is never relaunched. Use a new ID for each intentional action. Receipt files are
kept under `receipts/`; deleting them removes replay protection for those IDs.

- `state.json`: a snapshot at startup, command acknowledgement and job completion.
  Use a `status` command for a fresh snapshot, including benchmark progress.
- `job.json`: the most recent owned benchmark's ID, running/complete/cancelled/
  failed status, error and report path. Benchmark launch acknowledgement means
  accepted, not complete; the helper's `--wait` waits for this file.
- `capture.json`: screenshot completion status. The image is `<id>.png` in the
  same directory. Screenshot acknowledgement precedes the next rendered frame.
- `response.json`: acknowledgement/error for the most recently consumed request.

Use one command producer at a time: there is one incoming request slot. Keep
these local diagnostics out of performance-critical automated polling; routine
benchmark progress can be obtained with occasional status requests. The helper
writes desktop requests atomically and uses `devicectl device copy` on iOS.

Validation: `local_control_test.gd` checks validation, file consumption, forbidden
commands and persistent duplicate suppression. `local_control_integration_test.gd`
uses a real Metal game to check Settings navigation, complete/cancelled benchmark
jobs, profile/scale/cap restoration, screenshots and duplicate launch suppression.

Verified locally: protocol unit test, rendered Metal integration, Python client
status against a real app, and explicit rejection of a headless performance
benchmark. The signed iOS build succeeded and was installed on the paired phone.
Physical-device file control and benchmark execution are deferred: the phone was
locked when launch was attempted, and the user chose to return to that test later.

# MAC-12: Metal runtime investigation and owner capture handoff

## Result

Native ARM64, Godot 4.7.2 official (`ed1daf0bf`), Metal / Forward+ on
Apple Paravirtual (Apple5). Audio excluded: this VM has no audio device.

| Packaged run | Baseline fence timeouts | Fixed fence timeouts | Baseline shutdown error categories | Fixed shutdown error categories |
| --- | ---: | ---: | ---: | ---: |
| A: menu then normal exit | 0 | 0 | 0 | 0 |
| B: game, replay, normal exit | 1 | 0 | 3 | 0 |
| C: cached repeat | 2 | 2 | 3 | 0 |

Baseline: `9e92f54e2d695617e09c5b4b6316b166ebe6318f`.
Fixed package: `27591b41fcf32eede5917b4264f599ada46dc171`, clean source.
The three shutdown categories are leaked Texture RIDs, null RenderingServer
in ImageTexture destruction, and leaked TextureStorage allocations.
Final B/C each produced 55 replay frames, 54 hashes, zero mismatches and exit 0.
These runs are reproduction measurements, not a controlled performance benchmark.

## Confirmed cause and changes

Godot's macOS native cursor cache retains the resource supplied to
`Input.set_custom_mouse_cursor`. With an ImageTexture, conversion to native
cursor pixels calls `ImageTexture::get_image`, causing GPU readback, and the
cached texture can outlive RenderingServer. An isolated ImageTexture cursor
reproduced all three shutdown errors; the same pixels supplied as an Image did
not. Commit `77f21faa7cb62029c13e3bcd8185c51d17c11a0b` keeps native cursors
CPU-side. Commit `27591b41fcf32eede5917b4264f599ada46dc171` separates decoded
CPU images from GPU texture caches for radar/decal processing, avoiding further
unnecessary readback. No simulation flags, Metal sampler policy or fence timeout
settings changed. Native radar/cursor checks passed: five cases, 515 assertions;
build, Godot project parse, precommit and package audit passed.

## Remaining hypothesis — NOT a confirmed cause

Godot's Metal `FenceEvent::wait` waits on a shared-event value for 1000 ms and
logs `timeout waiting for fence` if it is not signalled in that interval. A
timeout alone does not prove the event is *never* signalled. The remaining
timeouts occur during game/replay entry, not just shutdown. Sampling after the
fixes showed shared-event waits under Godot main-loop/startup paths and extension
calls including `GameWorld::load_internal`, `AptMenuPlayer::drawCanvas`, terrain,
FX texture and W3D construction; the two repaired HUD paths were absent.
Sampling does not identify which of these waits actually timed out.

Working hypothesis: entry-time resource upload/render synchronization can stall
a Godot-managed command buffer/shared event beyond its one-second deadline,
possibly amplified by the VM GPU. Alternatives are an absent signal submission,
a failed command buffer, or an event-value/queue dependency error. No evidence
yet establishes an extension-owned fence left in flight or a permanent deadlock.

## Exact capture requested on the owner's real M5 Mac

1. Use the fixed ARM64 package above, Metal / Forward+, with the same retail
   installation and runner inputs. Record macOS, Xcode/Instruments, GPU and
   Godot versions, package identity, window dimensions and cache state. Keep
   normal rendering settings; do not switch to Compatibility, clear caches,
   change fence timeouts or enable validation for the first timing capture.
2. In Instruments choose **Metal System Trace**, targeting the packaged
   `OpenBFME.app` game executable (not the launcher). Capture CPU thread
   scheduling, Metal API activity, command-buffer submission/completion and GPU
   execution. Enable call stacks/symbolication where the installed Instruments
   version offers them; retain the matching executable and extension symbols.
   Run Time Profiler in a separate reproduction if that template does not expose
   CPU wait call stacks. Do not assume Time Profiler samples alone identify the
   timeout branch.
3. Start recording before launching/loading. Use these game arguments (the
   runner creates a current-build recording and exercises the replay menu):

   ```text
   --windowed -- --fps --perf --auto --advance=10 --map="maps/map wor rohan/map wor rohan.map" --faction=FactionMen --ai=2 --replay-menu-test
   ```

   Supply ROTWK_INSTALL and BFME2_INSTALL for the owner's paths. Retain the full
   stdout/stderr log with timestamps relative to capture start. Record initial
   world entry, replay menu Load, replay world entry and normal exit. Keep the
   trace running through exit. Repeat once without clearing caches; save each
   trace separately. If the runner is unavailable, use the same map/faction via
   the UI, record a fresh replay, load it through Replays, and document the
   changed method rather than substituting an old-build replay.
4. For each logged timeout, inspect the preceding one-second interval and its
   caller stack: identify the waiting CPU thread, shared-event value when
   exposed, associated queue/command buffer, submission time, GPU start/end,
   completion/error status and any signal operation. Determine whether the
   producer was submitted late, was running slowly, failed, or was never
   submitted. Follow cross-queue dependencies. If Instruments does not expose
   the event value/signal pairing, mark it unknown; do not infer it from nearby
   GPU activity. Keep several seconds of context around both loading transitions.
5. A late successful completion/signal supports the slow-frame hypothesis. An
   unscheduled producer, failed buffer, or signal for the wrong event/value
   supports a lifecycle/dependency bug. A clean real-M5 run only establishes
   non-reproduction there; it does not prove the VM is the cause.
6. If still ambiguous, a separate owner-authorized debugger run should break
   in `RenderingDeviceDriverMetal::FenceEvent::wait` only after
   `waitUntilSignaledValue` returns false, recording the stack, event/value and
   command-buffer status. Use matching Godot symbols/source; do not reuse an
   absolute address from a different executable. Metal API Validation is a
   separate diagnostic run because it changes timing. Preserve any validation
   errors without suppressing them.

Deliver the `.trace`, timestamped game log, exact reproduction arguments and
symbol/build identity privately. Traces/screenshots can contain retail art or
machine paths: do not commit them. No password sharing or further VM debugger
authorization is requested. The owner will run Metal/Instruments diagnostics.

## Limits

Fence timeouts remain unresolved. Final video covers replay Load/progression/exit,
not the entire initial navigation; initial world rendering was checked separately.
Window geometry was not identical across all runs. Exact cursor pixel/animation
parity and diagnostic getter compatibility were not independently validated.
There was no final full repository-wide test suite or audio validation.

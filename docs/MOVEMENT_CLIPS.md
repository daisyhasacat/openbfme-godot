# Recorded movement playback

Movement clips record the visual tracks in the current local match, then
move the models along those fixed tracks without running the battle again.
Use the Lua console:

```lua
bfme.movements.record({seconds=60})
-- Give the army its movement orders and let it move.
bfme.movements.finish()
bfme.movements.play({loop=true})
```

`play()` freezes simulation advancement and plays the clip from its start.
Pathfinding, collision handling, AI, production, combat, shroud updates and
scheduled Lua callbacks do not run. The render side interpolates recorded
transforms and animation frames, then draws the models normally. Camera
controls still work. Rendering and skeletal pose evaluation still cost time;
this is not video playback or a promise of a particular rendered FPS.

```lua
bfme.movements.pause()             -- pause the clip
bfme.movements.pause(false)        -- continue
bfme.movements.seek(10)            -- seconds from the clip's beginning
bfme.movements.play({speed=2})     -- restart at double speed
bfme.movements.status()            -- duration, time, samples, tracks, bytes, etc.
bfme.movements.stop()              -- restore the live scene and resume the battle
bfme.movements.clear()             -- stop playback and discard the clip
```

A non-looping clip holds its last frame until `stop()` or another command.
The live battle resumes from the state at playback start, not from the clip's
seek position. Lua gameplay writes are refused during playback. Scheduled
callbacks are held; command-list input accumulated while viewing is discarded
when playback stops, preserving orders that were queued before playback.

Capture options:

| Option | Default | Allowed values |
|---|---:|---|
| `seconds` | 60 | 0.2–300 |
| `interval` | 0.2 | 1/30–1 seconds between samples |
| `memory_mb` | 256 | 1–1024 MiB of estimated capture payload |

Capture stops explicitly with `stop_reason` set to `finished`,
`duration_limit` or `memory_limit`. Sample storage is bounded by the payload
budget; container/allocator overhead and playback models use additional
memory. Sampling happens on presented frames, so a slow rendered frame can
leave a longer interval; it does not synthesize unobserved simulation steps.
At least two samples are required for playback. `speed` accepts 0.01–16.

The clip is held in memory for this loaded match. Starting another recording
replaces it; unloading the match discards it. It is not saved to disk, a
retail command replay, a game save, or a way to resume the simulation from an
earlier time. It requires the original scene's loaded models and assets.
Network matches are refused.

The recorder captures the dynamic model instances, including battalion
members and buildings, with transforms, animation clip references and frame
inputs, blend and opacity. Model instance creation/removal follows sample
boundaries. Static map scenery remains in place. Team colours, hidden mesh
parts and controlled bone spins use each track's first recorded appearance;
this is a movement tool, not a complete reconstruction of changing cosmetics.
Transient particles and streaks are hidden during playback, then restored.
Terrain, HUD, minimap and game-state queries continue to describe the frozen
live match; the playback does not rebuild those systems. `get_render_pose`
reports the recorded visual position when its live object still exists.

The same controls are exposed to GDScript through
`GameWorld.movement_control(action, options)` and `movement_playing()`.

## Validation

`godot/tests/movement_clip_test.gd` checks visible motion with unchanged
simulation frame and state hash, seek/endpoints, pause, looping, resume,
threaded simulation, gameplay-write refusal, instance lifetimes, duration
limits, memory limits and clearing. The native Lua/timing suite checks API
dispatch, detached query results and the existing callback contracts.

An installed HD-profile test with 667 fighter battalions and 10,535 recorded
model tracks measured about 94.58 ms per live `advance` call and 1.17 ms per
playback `advance` call. These are single-run headless host-update timings,
with hashing outside the timed region, not end-to-end rendered FPS. Use
`MOVEMENT_HORDES=667` to repeat that scale in the installed-data test.

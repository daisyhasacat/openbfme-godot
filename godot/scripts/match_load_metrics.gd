## Observational startup timings. No simulation commands or synchronization waits.
extends Node

var game: Node
var phases := {}
var started_usec := 0
var first_draw_usec := 0
var last_draw_usec := 0
var probe_sent_usec := 0
var acknowledged_usec := 0
var frame_gaps_ms: Array[float] = []
var complete := false
var error := ""
var observation_seconds := 1.0

func _ready() -> void:
	RenderingServer.frame_post_draw.connect(_drawn)

func mark(phase: String) -> void:
	var now := Time.get_ticks_usec()
	if started_usec == 0: started_usec = now
	phases[phase] = float(now - started_usec) / 1000.0

func _process(_delta: float) -> void:
	if complete: return
	if phases.has("playing") and float(Time.get_ticks_usec() - started_usec) / 1000.0 - float(phases.playing) > 60000.0:
		error = "Startup did not produce a rendered world frame and responsive controls within 60 seconds."
		complete = true
		return
	if first_draw_usec == 0: return
	if game._state != game.State.PLAYING or not is_instance_valid(game._hud): return
	acknowledged_usec = game._hud.get_startup_probe_usec()
	if acknowledged_usec == 0:
		if probe_sent_usec == 0: probe_sent_usec = Time.get_ticks_usec()
		var probe := InputEventAction.new()
		probe.action = "openbfme_startup_probe"
		get_viewport().push_input(probe)
	if Time.get_ticks_usec() - first_draw_usec > 30000000:
		error = "Gameplay input did not acknowledge the startup probe within 30 seconds."
		complete = true

func _drawn() -> void:
	if complete or not phases.has("world_exposed") or not game._world.is_visible_in_tree() or not game._camera.current: return
	var now := Time.get_ticks_usec()
	if first_draw_usec == 0:
		first_draw_usec = now
		mark("first_world_frame")
	else:
		frame_gaps_ms.append(float(now - last_draw_usec) / 1000.0)
	last_draw_usec = now
	if acknowledged_usec > 0 and float(now - first_draw_usec) / 1000000.0 >= observation_seconds:
		complete = true

func result() -> Dictionary:
	var out := {"status": "complete" if complete and error.is_empty() else "failed", "error": error,
		"phases_ms": phases.duplicate(), "map_load_ms": phases.get("map_load_complete", 0.0) - phases.get("map_load_started", 0.0),
		"render_available": DisplayServer.get_name() != "headless",
		"definition": "First world frame = frame_post_draw with loaded world exposed and gameplay camera current (render submission, not GPU completion). Controls = harmless Viewport input probe acknowledged through the HUD queue after PLAYING, with input enabled and no modal. Frame gaps include post-reveal stalls. Warm process caches; not a cold application launch."}
	if first_draw_usec > 0 and acknowledged_usec > 0:
		out["request_to_first_world_frame_ms"] = float(first_draw_usec - started_usec) / 1000.0
		out["request_to_controls_ms"] = float(acknowledged_usec - started_usec) / 1000.0
		out["world_frame_to_controls_ms"] = float(acknowledged_usec - first_draw_usec) / 1000.0
		out["input_queue_ms"] = float(acknowledged_usec - probe_sent_usec) / 1000.0
		out["post_reveal_frame_gaps_ms"] = frame_gaps_ms.duplicate()
		out["post_reveal_max_frame_gap_ms"] = frame_gaps_ms.max() if not frame_gaps_ms.is_empty() else 0.0
	return out

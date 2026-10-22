## Persistent iOS match resume. Two slots + atomic marker keep the last complete
## checkpoint intact when suspension/termination interrupts a write.
extends Node
const INTERVAL_SECONDS := 30.0
var game: Node
var directory := "user://quick-resume"
var enabled := OS.get_name() == "iOS"
var restoring := false
var _elapsed := 0.0
var _background := false
var _writing := false
var _last_frame := -1
var _last_save_ms := -2000
var _attempted := false
var last_result: Dictionary = {}

func _path(name: String) -> String:
	return ProjectSettings.globalize_path(directory.path_join(name))

func _read(name: String) -> Dictionary:
	var file := FileAccess.open(_path(name), FileAccess.READ)
	if file == null or file.get_length() > 1048576: return {}
	# No object deserialization. Binary Variants retain exact camera float bits.
	var value = file.get_var(false)
	file.close()
	return value if value is Dictionary else {}

func _write(name: String, state: Dictionary) -> Error:
	var path := _path(name)
	var file := FileAccess.open(path + ".tmp", FileAccess.WRITE)
	if file == null: return FileAccess.get_open_error()
	file.store_var(state, false)
	file.flush()
	var error := file.get_error()
	file.close()
	if error != OK: return error
	return DirAccess.rename_absolute(path + ".tmp", path)

func can_capture() -> bool:
	return enabled and not restoring and not _writing and is_instance_valid(game) and game._state == game.State.PLAYING and game._net_state.is_empty() and not game._replay_active and not game._online_spectate and game._game_kind != "replay" and not (is_instance_valid(game._benchmark_tool) and game._benchmark_tool.running)

func capture(reason := "periodic") -> Dictionary:
	if not can_capture(): return {"ok": false, "error": "No resumable single-player match"}
	var frame: int = game._world.get_frame()
	var now := Time.get_ticks_msec()
	if frame == _last_frame and now - _last_save_ms < 1500:
		return {"ok": true, "unchanged": true}
	_writing = true
	var result := _capture(reason)
	_writing = false
	last_result = result
	if result.get("ok", false):
		_last_frame = int(result.frame)
		_last_save_ms = now
		_elapsed = 0.0
		print("QUICK RESUME saved frame %d (%s) in %d ms" % [result.frame, reason, Time.get_ticks_msec() - now])
	else:
		push_warning("Quick resume save failed: " + str(result.get("error", "unknown error")))
	return result

func _capture(reason: String) -> Dictionary:
	var error := DirAccess.make_dir_recursive_absolute(_path(""))
	if error != OK: return {"ok": false, "error": error_string(error)}
	var previous := _read("current.state")
	var slot := "b" if previous.get("slot", "") == "a" else "a"
	var kind := 1 if game._campaign_flow != null and game._campaign_flow.active else 2
	var saved: Dictionary = game._world.save_game(_path(slot + ".obfmesave"), {"description": "iOS Quick Resume", "kind": kind})
	if not saved.get("ok", false): return saved
	var camera: Dictionary = game._hud.get_camera() if is_instance_valid(game._hud) else {}
	var selected: PackedInt64Array = game._hud.get_selection() if is_instance_valid(game._hud) else PackedInt64Array()
	var state := {"version": 1, "slot": slot, "generation": "%d-%d" % [int(Time.get_unix_time_from_system()), Time.get_ticks_usec()],
		"frame": int(saved.frame), "hash": int(saved.hash), "camera": camera, "selection": selected,
		"paused": game._world.is_paused(), "quit_open": game._quit_open, "reason": reason}
	error = _write("current.state", state)
	if error != OK: return {"ok": false, "error": "Cannot publish resume checkpoint: " + error_string(error)}
	return saved

func _process(delta: float) -> void:
	if not enabled or _background or restoring: return
	if not can_capture():
		_elapsed = 0.0
		return
	_elapsed += delta
	if _elapsed >= INTERVAL_SECONDS:
		_elapsed = 0.0 # Also limit retries after an unsupported snapshot/disk failure.
		capture()

func _notification(what: int) -> void:
	if not enabled: return
	if what in [NOTIFICATION_APPLICATION_FOCUS_OUT, NOTIFICATION_APPLICATION_PAUSED]:
		_background = true
		# Must finish in this callback; deferred work may never run on iOS.
		capture("background")
	elif what in [NOTIFICATION_APPLICATION_FOCUS_IN, NOTIFICATION_APPLICATION_RESUMED]:
		_background = false

func entered_game() -> void:
	_elapsed = 0.0
	_last_frame = -1
	if not enabled or restoring: return
	# A different match must never retain the old match's resume marker.
	discard()
	if can_capture(): capture("match start")

func discard() -> void:
	_elapsed = 0.0
	_last_frame = -1
	if not enabled: return
	DirAccess.remove_absolute(_path("current.state"))
	DirAccess.remove_absolute(_path("attempt.state"))

func _notice(message: String) -> void:
	print("QUICK RESUME ", message)
	if is_instance_valid(game._lua_console): game._lua_console._append(message + "\n")
	var release := get_tree().root.get_node_or_null("Release")
	if release != null: release.set_update_note(message)

func try_restore() -> bool:
	if not enabled or _attempted: return false
	_attempted = true
	var state := _read("current.state")
	if state.is_empty(): return false
	if state.get("version", 0) != 1 or state.get("slot", "") not in ["a", "b"] or not state.get("generation", "") is String or str(state.get("generation", "")).is_empty():
		_notice("Quick resume checkpoint is invalid; starting at the main menu.")
		return false
	var attempted := _read("attempt.state")
	if attempted.get("generation", "") == state.generation:
		_notice("The previous quick resume did not finish. Automatic retry is disabled; its save is retained for recovery.")
		return false
	if _write("attempt.state", {"generation": state.generation}) != OK:
		_notice("Cannot prepare quick resume; starting at the main menu.")
		return false
	var path := _path(str(state.slot) + ".obfmesave")
	var check: Dictionary = game._world.check_save(path)
	if not check.get("ok", false):
		_notice("Quick resume could not load: " + str(check.get("error", "invalid save")))
		return false
	restoring = true
	game._save_flow.last_load = {}
	await game._save_flow.load_save(path)
	var result: Dictionary = game._save_flow.last_load
	if result.get("state", "") != "ready" or game._state != game.State.PLAYING or game._world.get_frame() != int(state.get("frame", -1)) or game._world.get_state_hash() != int(state.get("hash", -1)):
		restoring = false
		_notice("Quick resume failed; the checkpoint is retained and will not be retried automatically.")
		return false
	_restore_view(state)
	DirAccess.remove_absolute(_path("attempt.state"))
	restoring = false
	_elapsed = 0.0
	_last_frame = -1
	_notice("Resumed your previous game.")
	return true

func _restore_view(state: Dictionary) -> void:
	if is_instance_valid(game._hud):
		var camera: Dictionary = state.get("camera", {})
		if camera.has("position"): game._hud.camera_look_at(camera.position)
		if camera.has("height_above_ground"): game._hud.camera_set_height(camera.height_above_ground)
		if camera.has("angle") and camera.has("pitch"): game._hud.camera_set_angle_pitch(camera.angle, camera.pitch)
		game._hud.select_objects(state.get("selection", PackedInt64Array()))
	if state.get("quit_open", false): game._toggle_quit_menu()
	game._world.set_paused(bool(state.get("paused", false)))

## Development-only, local-file diagnostics. No listener, eval or script execution.
extends Node

const COMMANDS := ["status", "settings", "benchmark_start", "benchmark_cancel", "screenshot", "menu_input", "camera"]
var game: Node
var directory := "user://local-control"
var _next_poll := 0
var _owned_tool: Node
var _job_id := ""
var _profile_before = null
var _profile_changed := false
var _scale_before := 1.0
var _scale_changed := false
var _invalid_since := 0

func _ready() -> void:
	if not OS.is_debug_build():
		set_process(false)
		return
	var error := DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(directory.path_join("receipts")))
	if error != OK:
		push_warning("LOCAL CONTROL cannot create its sandbox directory: " + str(error))
		set_process(false)
		return
	var previous_path := directory.path_join("job.json")
	if FileAccess.file_exists(previous_path):
		var previous = JSON.parse_string(FileAccess.get_file_as_string(previous_path))
		if previous is Dictionary and previous.get("status") == "running":
			previous.status = "failed"
			previous["error"] = "App restarted during diagnostic job."
			_write("job.json", previous)
	_write("state.json", snapshot())
	print("LOCAL CONTROL ready: ", ProjectSettings.globalize_path(directory))

func _process(_delta: float) -> void:
	if not _job_id.is_empty() and (not is_instance_valid(_owned_tool) or not _owned_tool.running):
		_restore_overrides()
		_write("job.json", {"id": _job_id, "status": _owned_tool.report.get("status", "failed") if is_instance_valid(_owned_tool) else "failed",
			"error": _owned_tool.report.get("error", "") if is_instance_valid(_owned_tool) else "Benchmark tool was closed.", "report_path": _owned_tool._path if is_instance_valid(_owned_tool) else ""})
		_owned_tool = null
		_job_id = ""
		_write("state.json", snapshot())
	if Time.get_ticks_msec() < _next_poll: return
	_next_poll = Time.get_ticks_msec() + 250
	var path := directory.path_join("request.json")
	if not FileAccess.file_exists(path): return
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null: return
	if file.get_length() > 32768:
		file.close()
		DirAccess.remove_absolute(path)
		_write("response.json", {"id": "", "status": "error", "error": "Request exceeds 32 KiB."})
		return
	var parser := JSON.new()
	var parsed := parser.parse(file.get_as_text())
	var request = parser.data if parsed == OK else null
	file.close()
	# A device copy can briefly expose an incomplete file. Allow it to finish.
	if not request is Dictionary:
		if _invalid_since == 0: _invalid_since = Time.get_ticks_msec()
		if Time.get_ticks_msec() - _invalid_since < 2000: return
	_invalid_since = 0
	if DirAccess.remove_absolute(path) != OK: return
	if not request is Dictionary:
		_write("response.json", {"id": "", "status": "error", "error": "Expected a JSON object."})
		return
	var response := execute(request)
	if not str(response.id).is_empty() and response.status != "duplicate":
		_write("receipts/" + str(response.id) + ".json", response)
	_write("response.json", response)
	_write("state.json", snapshot())

func snapshot() -> Dictionary:
	var out := {"version": 1, "development_build": OS.is_debug_build(), "commands": COMMANDS,
		"state": str(game.State.find_key(game._state)), "benchmark_running": false}
	if is_instance_valid(game._shell): out["shell_stack"] = game._shell.shell_stack()
	if is_instance_valid(game._benchmark_tool):
		out.benchmark_running = game._benchmark_tool.running
		out["benchmark_status"] = game._benchmark_tool.report.get("status", "idle")
		out["benchmark_progress"] = game._benchmark_tool._status.text
		out["report_path"] = game._benchmark_tool._path
	return out

func execute(request: Dictionary) -> Dictionary:
	var id = request.get("id", "")
	if not id is String or not _valid_id(id): return {"id": "", "status": "error", "error": "id must be 1–64 letters, digits, hyphens or underscores."}
	var receipt := directory.path_join("receipts").path_join(id + ".json")
	if FileAccess.file_exists(receipt):
		return {"id": id, "status": "duplicate", "previous": JSON.parse_string(FileAccess.get_file_as_string(receipt))}
	var out := {"id": id, "status": "ok"}
	if not OS.is_debug_build(): return _error(out, "Local control is disabled in release builds.")
	if request.get("version", 0) != 1: return _error(out, "Unsupported protocol version.")
	var command: String = str(request.get("command", ""))
	if not COMMANDS.has(command): return _error(out, "Unknown command.")
	# Acknowledge consumption before a long operation. Never replay a launch.
	if not _write("receipts/" + id + ".json", {"id": id, "status": "accepted"}): return _error(out, "Cannot record request consumption.")
	match command:
		"status": out["state"] = snapshot()
		"benchmark_cancel":
			if is_instance_valid(game._benchmark_tool) and game._benchmark_tool.running: game._benchmark_tool.cancelled = true
		"benchmark_start":
			var why := _start_benchmark(request, id)
			if not why.is_empty(): return _error(out, why)
			out.status = "accepted"
			out["job_id"] = id
		"settings":
			if _benchmark_running() or game._state != game.State.MENU: return _error(out, "Settings control requires an idle main menu.")
			var tab: String = str(request.get("tab", "Graphics"))
			if not game._display_screen.TABS.has(tab): return _error(out, "Unknown Settings tab.")
			if not game._display_screen.is_open() and not game._shell.open_options(false, false, false, false): return _error(out, "Could not open Settings.")
			game._display_screen._show_tab(tab)
		"screenshot":
			if DisplayServer.get_name() == "headless": return _error(out, "Screenshots require rendering.")
			_capture(id)
			out.status = "accepted"
			out["path"] = directory.path_join(id + ".png")
		"menu_input":
			if _benchmark_running() or (game._state not in [game.State.MENU, game.State.SCORE] and not game._quit_open): return _error(out, "Menu input requires a menu; gameplay commands are not supported.")
			var keys := {"up": KEY_UP, "down": KEY_DOWN, "left": KEY_LEFT, "right": KEY_RIGHT, "accept": KEY_ENTER, "back": KEY_ESCAPE}
			var action: String = str(request.get("action", ""))
			if not keys.has(action): return _error(out, "Unknown menu action.")
			for pressed in [true, false]:
				var event := InputEventKey.new()
				event.keycode = keys[action]
				event.pressed = pressed
				get_viewport().push_input(event)
		"camera":
			if _benchmark_running() or game._state != game.State.PLAYING or not is_instance_valid(game._hud): return _error(out, "Camera control requires an active game outside a benchmark.")
			for key in ["x", "y", "height"]:
				if not _number(request.get(key)) or not is_finite(float(request[key])) or absf(float(request[key])) > 100000: return _error(out, "Camera coordinates must be finite numbers within 100000 units.")
			if float(request.height) <= 0: return _error(out, "Camera height must be positive.")
			game._hud.camera_look_at(Vector2(float(request.x), float(request.y)))
			game._hud.camera_set_height(float(request.height))
			out["camera"] = game._hud.get_camera()
	return out

func _start_benchmark(request: Dictionary, id: String) -> String:
	if _benchmark_running() or game._state != game.State.MENU: return "Benchmark requires an idle main menu."
	var seconds = request.get("seconds", 10)
	if not _number(seconds) or not is_finite(float(seconds)) or float(seconds) < 3 or float(seconds) > 60: return "seconds must be between 3 and 60."
	if not request.get("uncapped", true) is bool: return "uncapped must be a boolean."
	if request.has("shadow_profile") and request.shadow_profile not in ["auto", "balanced", "high"]: return "Unknown shadow profile."
	if request.has("render_scale"):
		if not _number(request.render_scale) or not is_finite(float(request.render_scale)) or float(request.render_scale) < 0.5 or float(request.render_scale) > 1.0: return "render_scale must be between 0.5 and 1.0."
	if game._display_screen.is_open(): game._display_screen.close(false)
	game._open_benchmark()
	var tool = game._benchmark_tool
	var reason: String = tool.unavailable_reason()
	if not reason.is_empty(): return reason
	if request.has("shadow_profile"):
		_profile_before = game._shell.get_option("OpenBFMEShadowProfile")
		if not game._shell.set_option("OpenBFMEShadowProfile", request.shadow_profile): return "Could not apply shadow override."
		_profile_changed = true
	if request.has("render_scale"):
		_scale_before = get_viewport().scaling_3d_scale
		get_viewport().scaling_3d_scale = float(request.render_scale)
		_scale_changed = true
	tool._duration.value = float(seconds)
	tool._uncapped.button_pressed = request.get("uncapped", true)
	_owned_tool = tool
	_job_id = id
	if not _write("job.json", {"id": id, "status": "running"}):
		_restore_overrides()
		_owned_tool = null
		_job_id = ""
		return "Could not publish benchmark job."
	tool.start()
	tool.report["diagnostic_overrides"] = request.duplicate(true)
	return ""

func _benchmark_running() -> bool:
	return is_instance_valid(game._benchmark_tool) and game._benchmark_tool.running

func _restore_overrides() -> void:
	if _profile_changed and is_instance_valid(game._shell):
		game._shell.set_option("OpenBFMEShadowProfile", "" if _profile_before == null else str(_profile_before))
		_profile_changed = false
		if is_instance_valid(game._sun):
			preload("res://scripts/shadow_profile.gd").apply(game._sun, game._game_options.current().OpenBFMEShadowProfile, int(game._shell.get_detail_state().get("shadow_lod", 3)))
	if _scale_changed and is_inside_tree():
		get_viewport().scaling_3d_scale = _scale_before
		_scale_changed = false

func _capture(id: String) -> void:
	await RenderingServer.frame_post_draw
	var error := get_viewport().get_texture().get_image().save_png(directory.path_join(id + ".png"))
	_write("capture.json", {"id": id, "status": "complete" if error == OK else "failed", "error": error})

func _write(name: String, value: Dictionary) -> bool:
	var file := FileAccess.open(directory.path_join(name), FileAccess.WRITE)
	if file == null:
		push_warning("LOCAL CONTROL could not write " + name)
		return false
	file.store_string(JSON.stringify(value, "\t"))
	return true

func _error(response: Dictionary, reason: String) -> Dictionary:
	response.status = "error"
	response["error"] = reason
	return response

static func _valid_id(id: String) -> bool:
	if id.is_empty() or id.length() > 64: return false
	for character in id:
		if not "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-".contains(character): return false
	return true

static func _number(value: Variant) -> bool:
	return typeof(value) in [TYPE_INT, TYPE_FLOAT]

func _exit_tree() -> void:
	_restore_overrides()

## Settings benchmark. Owns a temporary viewer match; never enters the user's
## match/score/replay flows. All scenarios run with the same saved settings.
extends CanvasLayer

const Plan := preload("res://scripts/benchmark_plan.gd")
const Sampler := preload("res://scripts/console_performance.gd")
const PRESET := "res://benchmarks/men.json"
const LAST_REPORT := "user://benchmarks/latest-settings.json"

var game
var running := false
var cancelled := false
var report: Dictionary = {}
var warmup_seconds := 3.0
var capture_seconds := 10.0
var allow_headless := false # automated validation only; results still identify missing GPU timings
var _preset: Dictionary
var _shade: ColorRect
var _theme: Theme
var _title: Label
var _panel: PanelContainer
var _progress: VBoxContainer
var _status: Label
var _results: RichTextLabel
var _start: Button
var _close: Button
var _cancel: Button
var _uncapped: CheckBox
var _duration: SpinBox
var _sampler
var _logger
var _finishing := false
var _scene: Node3D
var _hud: Node
var _camera: Camera3D
var _world_loaded := false
var _game_suspended := false
var _old_process := true
var _old_shell_process := true
var _old_shell_visible := true
var _old_world_visible := true
var _old_camera := false
var _old_max_fps := 0
var _old_vsync := DisplayServer.VSYNC_ENABLED
var _old_logic_thread := true
var _caps_changed := false
var _player := -1
var _center := Vector2.ZERO
var _army_origin := Vector2.ZERO
var _spacing := 100.0
var _columns := 1
var _spawned := 0
var _roster := {}
var _unit_ids: Array[int] = []
var _environment_before: Environment
var _sun_was_visible := false
var _path := ""
var _stage_error := ""

func _ready() -> void:
	layer = 115
	process_mode = Node.PROCESS_MODE_ALWAYS
	var blocker := Control.new()
	blocker.set_meta("openbfme_native_menu", true)
	blocker.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	blocker.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(blocker)
	_theme = Theme.new()
	blocker.theme = _theme
	_shade = ColorRect.new()
	_shade.color = Color(0, 0, 0, 0.65)
	_shade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	blocker.add_child(_shade)
	_shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_panel = PanelContainer.new()
	blocker.add_child(_panel)
	_panel.set_anchors_preset(Control.PRESET_CENTER)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_panel.add_child(scroll)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(column)
	_title = Label.new()
	_title.text = "Performance benchmark"
	column.add_child(_title)
	var description := Label.new()
	description.text = "Main menu → Skirmish lobby → Men base + 1,000 CP → Men base + 20,000 CP.\n12 buildings, mixed idle Men battalions and siege, fixed cameras. Loading and spawning are excluded.\nUses saved settings; apply any changes before starting. The 20,000 CP setup may take several minutes.\nCancel at any time. Returns to the main menu; results are saved locally."
	description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(description)
	var options := HFlowContainer.new()
	column.add_child(options)
	_duration = SpinBox.new()
	_duration.min_value = 3
	_duration.max_value = 60
	_duration.value = capture_seconds
	_duration.suffix = "s per scenario"
	options.add_child(_duration)
	_uncapped = CheckBox.new()
	_uncapped.text = "Uncap FPS and disable VSync"
	_uncapped.button_pressed = true
	options.add_child(_uncapped)
	_results = RichTextLabel.new()
	_results.selection_enabled = true
	_results.custom_minimum_size = Vector2(0, 180)
	_results.size_flags_vertical = Control.SIZE_EXPAND_FILL
	column.add_child(_results)
	var buttons := HBoxContainer.new()
	column.add_child(buttons)
	_start = Button.new()
	_start.text = "Run benchmark"
	_start.pressed.connect(start)
	buttons.add_child(_start)
	_close = Button.new()
	_close.text = "Close"
	_close.pressed.connect(func(): if not running: queue_free())
	buttons.add_child(_close)
	_progress = VBoxContainer.new()
	blocker.add_child(_progress)
	_progress.position = Vector2(18, 45)
	_status = Label.new()
	_status.add_theme_color_override("font_outline_color", Color.BLACK)
	_status.add_theme_constant_override("outline_size", 5)
	_progress.add_child(_status)
	_cancel = Button.new()
	_cancel.text = "Cancel benchmark"
	_cancel.pressed.connect(func(): cancelled = true)
	_progress.add_child(_cancel)
	_progress.hide()
	_layout_dialog()
	get_viewport().size_changed.connect(_layout_dialog)
	_preset = JSON.parse_string(FileAccess.get_file_as_string(PRESET))
	if FileAccess.file_exists(LAST_REPORT):
		var last = JSON.parse_string(FileAccess.get_file_as_string(LAST_REPORT))
		if last is Dictionary:
			report = last
			_show_results()
	var reason := unavailable_reason()
	_start.disabled = not reason.is_empty()
	if not reason.is_empty(): _results.text = reason
	_close.grab_focus()

func _layout_dialog() -> void:
	var viewport := get_viewport().get_visible_rect().size
	var scale := clampf(minf(viewport.x / 1500.0, viewport.y / 950.0), 0.85, 1.75)
	_theme.default_font_size = roundi(18.0 * scale)
	_title.add_theme_font_size_override("font_size", roundi(28.0 * scale))
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.035, 0.055, 0.045, 0.99)
	style.border_color = Color(0.42, 0.5, 0.32)
	style.set_border_width_all(2)
	style.set_corner_radius_all(roundi(8.0 * scale))
	for side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]: style.set_content_margin(side, 20.0 * scale)
	_panel.add_theme_stylebox_override("panel", style)
	_panel.size = Vector2(minf(940.0 * scale, viewport.x - 32.0), minf(650.0 * scale, viewport.y - 32.0))
	_panel.position = (viewport - _panel.size) * 0.5
	_duration.custom_minimum_size = Vector2(250.0 * scale, 48.0 * scale)
	_results.custom_minimum_size.y = 200.0 * scale
	for button in [_start, _close, _cancel]: button.custom_minimum_size.y = 48.0 * scale
	_progress.size.x = minf(viewport.x - 36.0, 900.0 * scale)

func unavailable_reason() -> String:
	if not is_instance_valid(game) or game._state != game.State.MENU:
		return "Return to the main menu before running the benchmark."
	if game._fs.is_bfme1(): return "The Men benchmark requires BFME2 or RotWK data."
	if DisplayServer.get_name() == "headless" and not allow_headless:
		return "A graphical renderer is required for a performance benchmark."
	for screen in game._shell.shell_stack():
		if screen not in ["MainMenu.apt", "Options.apt"]:
			return "Leave the lobby and return to the main menu before running the benchmark."
	return ""

func _input(event: InputEvent) -> void:
	if running:
		if event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
			cancelled = true
		if event is InputEventJoypadButton and event.pressed and event.button_index == JOY_BUTTON_B:
			cancelled = true
		# Keep input out of the measured menu/HUD; Cancel still uses native UI.
		if event is InputEventKey or event is InputEventJoypadButton or event is InputEventJoypadMotion:
			get_viewport().set_input_as_handled()

func _notification(what: int) -> void:
	if running and what == NOTIFICATION_APPLICATION_FOCUS_OUT:
		cancelled = true # an unfocused/throttled window cannot produce a valid run

func _wait(seconds: float) -> bool:
	var end := Time.get_ticks_usec() + int(seconds * 1000000.0)
	while Time.get_ticks_usec() < end:
		if cancelled: return false
		await get_tree().process_frame
	return not cancelled

func start() -> void:
	if running: return
	var reason := unavailable_reason()
	if not reason.is_empty():
		_results.text = reason
		return
	running = true
	_finishing = false
	cancelled = false
	_logger = preload("res://scripts/walk_error_logger.gd").new()
	OS.add_logger(_logger)
	report = {"version": 1, "status": "running", "preset": _preset.duplicate(true), "stages": [],
		"created_utc": Time.get_datetime_string_from_unix_time(int(Time.get_unix_time_from_system())),
		"build": ClassDB.instantiate("ReleaseInfo").version_line(),
		"platform": OS.get_name(), "cpu": OS.get_processor_name(), "logical_processors": OS.get_processor_count(),
		"gpu": RenderingServer.get_video_adapter_name(), "renderer": RenderingServer.get_current_rendering_method(),
		"driver": RenderingServer.get_current_rendering_driver_name(), "resolution": [get_viewport().get_visible_rect().size.x, get_viewport().get_visible_rect().size.y],
		"render_scale": get_viewport().scaling_3d_scale, "menu_style": game._menu_style,
		"note": "Idle synthetic Men workload. Combat auto-acquisition disabled; units are invulnerable to keep CP fixed. Wall-frame intervals include waiting. CPU/GPU scopes overlap; do not sum them. Unavailable GPU timings are omitted."}
	capture_seconds = _duration.value
	if game._display_screen.is_open(): game._display_screen.close(false)
	_old_max_fps = Engine.max_fps
	_old_vsync = DisplayServer.window_get_vsync_mode()
	_old_logic_thread = game._world.get_logic_thread()
	report["saved_display"] = game._display.to_dict()
	report["game_options"] = game._game_options.current()
	report["uncapped"] = _uncapped.button_pressed
	report["frame_cap_before"] = _old_max_fps
	report["vsync_before"] = int(_old_vsync)
	if _uncapped.button_pressed:
		Engine.max_fps = 0
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	_caps_changed = true
	_panel.hide()
	_shade.hide()
	_progress.show()
	_cancel.grab_focus()
	_sampler = Sampler.new()
	_sampler.world = game._world
	_sampler.hud = func() -> Node: return _hud
	_sampler.hide()
	add_child(_sampler)
	# The legacy Options wrapper can remain after its OpenBFME page closes.
	if not await _main_menu():
		await _finish("Could not return to the main menu.")
		return
	if not await _measure("main_menu", {}):
		await _finish("Cancelled or no main-menu samples.")
		return
	_status.text = "Opening Skirmish lobby…"
	if not game._shell.shell_fscommand("AptMainMenu::Skirmish", ""):
		await _finish("Could not open the Skirmish lobby.")
		return
	if not await _wait(2.0):
		await _finish("Cancelled.")
		return
	if game._shell.shell_stack().is_empty() or game._shell.shell_stack()[-1] != "Skirmish.apt":
		await _finish("The Skirmish lobby did not open.")
		return
	if not await _measure("lobby", {}):
		await _finish("Cancelled or no lobby samples.")
		return
	var error: String = await _prepare_world()
	if not error.is_empty():
		await _finish(error)
		return
	for target in [int(_preset.normal_cp), int(_preset.stress_cp)]:
		error = await _populate(target)
		if not error.is_empty():
			await _finish(error)
			return
		var metadata := {"cp": target, "groups_and_siege": _spawned, "roster": _roster.duplicate(true),
			"buildings": _preset.buildings.size(), "world_objects": game._world.get_object_count()}
		if not await _measure("men_normal" if target == int(_preset.normal_cp) else "men_20000_cp", metadata):
			await _finish(_stage_error if not _stage_error.is_empty() else "Cancelled or no in-game samples.")
			return
		# Stop the worker before the next preparation; the timed sections use
		# the original logic-thread setting, while creation happens in batches.
		game._world.set_auto_advance(false)
		game._world.set_logic_thread(false)
		if target == int(_preset.normal_cp):
			error = _clear_world()
			if error.is_empty(): error = await _prepare_world()
			if not error.is_empty():
				await _finish(error)
				return
	await _finish("")

func _main_menu() -> bool:
	for attempt in 300:
		if cancelled and not _finishing: return false
		var stack: PackedStringArray = game._shell.shell_stack()
		if stack == PackedStringArray(["MainMenu.apt"]) and game._shell.has_level(game._shell.shell_top_level()):
			return true
		if stack.is_empty(): game._shell.shell_push("MainMenu.apt")
		elif stack[-1] != "MainMenu.apt": game._shell.shell_pop()
		game._shell.tick(0.033)
		await get_tree().process_frame
	return false

func _process(_delta: float) -> void:
	if running and _logger != null and _logger.script_errors() > 0:
		cancelled = true
		_finish("Benchmark script failed: " + _logger.first_error())
		return
	if running and _world_loaded:
		game._world.update_lua()

func _measure(stage: String, workload: Dictionary) -> bool:
	_stage_error = ""
	_status.text = "%s · warming up…" % stage.replace("_", " ").capitalize()
	if not await _wait(warmup_seconds): return false
	if workload.has("cp") and _used_cp() != int(workload.cp):
		_stage_error = "%s changed during warmup: requested %d CP, measured %d CP." % [stage, int(workload.cp), _used_cp()]
		return false
	var frame_before: int = game._world.get_presented_frame() if _world_loaded else 0
	_sampler.start_capture(capture_seconds)
	while running and is_instance_valid(_sampler) and _sampler.capturing:
		if cancelled:
			_sampler.finish_capture()
			return false
		_status.text = "%s · %.1f / %.0f s" % [stage.replace("_", " ").capitalize(), _sampler._elapsed, capture_seconds]
		await get_tree().process_frame
	if not running or cancelled or not is_instance_valid(_sampler) or _sampler.samples.is_empty(): return false
	var captured: Dictionary = _sampler.last_report.duplicate(true)
	if workload.has("cp"):
		workload["cp_after"] = _used_cp()
		if int(workload.cp_after) != int(workload.cp):
			_stage_error = "%s changed during capture: requested %d CP, measured %d CP." % [stage, int(workload.cp), int(workload.cp_after)]
			return false
		workload["logic_frames"] = game._world.get_presented_frame() - frame_before
	captured["shell_stack"] = game._shell.shell_stack()
	captured["stage"] = stage
	captured["workload"] = workload
	captured["frames_summary"] = Plan.frame_summary(_sampler.samples)
	report.stages.append(captured)
	return true

func _prepare_world() -> String:
	_status.text = "Loading the benchmark map…"
	if not _game_suspended:
		_old_process = game.is_processing()
		_old_shell_process = game._shell.auto_process
		_old_shell_visible = game._shell.visible
		_old_world_visible = game._world.visible
		_old_camera = game._camera.current
		game.set_process(false)
		game._shell.auto_process = false
		game._shell.hide()
		game._shell_leave_for_game()
		_game_suspended = true
	game._world.set_auto_advance(false)
	game._world.set_logic_thread(false)
	await get_tree().process_frame
	var loaded: Dictionary = game._world.load_map(_preset.map, {"terrain": true, "seed": int(_preset.seed),
		"logic_thread": false, "slots": [{"player": "Player_1", "faction": _preset.faction, "human": true, "team": 0, "start_index": 0}],
		"animations": true, "house_colors": true, "texture_animation": true})
	_world_loaded = true
	if not loaded.get("ok", false): return "Map load failed: %s" % loaded.get("errors", [])
	var bounds: Dictionary = loaded.get("terrain", {}).get("map_info", {})
	if not bounds.has("width") or not bounds.has("height"): return "The benchmark map has no terrain dimensions."
	var playable := Vector2(float(bounds.width) - 2.0 * float(bounds.get("border", 0)), float(bounds.height) - 2.0 * float(bounds.get("border", 0))) * 10.0
	_center = playable * 0.5
	report["map_info"] = bounds
	report["load_stops"] = loaded.get("stops", [])
	report["terrain_errors"] = loaded.get("terrain_errors", [])
	for player in game._world.get_economy().get("players", []):
		if player.name == "Player_1": _player = int(player.index)
	if _player < 0: return "The benchmark's Men player is missing."
	# Definition-only queries are done during preparation, never inside capture.
	var costs: Array = []
	for name in _preset.units:
		var query: Dictionary = game._world.execute_lua("local t=bfme.templates[%s]; assert(t,'Missing Men unit'); return t.commandPoints" % JSON.stringify(name))
		if not query.get("ok", false): return "Could not read %s: %s" % [name, query.get("output", "")]
		var value: String = str(query.output).strip_edges()
		if not value.is_valid_int() or int(value) <= 0: return "%s has no positive CommandPoints value." % name
		costs.append({"name": name, "cp": int(value)})
	_preset["unit_costs"] = costs
	report.preset = _preset.duplicate(true)
	var stress: Dictionary = Plan.army(int(_preset.stress_cp), costs)
	if not stress.ok: return stress.error
	_columns = ceili(sqrt(float(stress.units.size())))
	var half_army := Vector2.ONE * float(_columns - 1) * _spacing * 0.5
	var army_min := _center + Vector2(650, 0) - half_army - Vector2.ONE * 80.0
	var army_max := _center + Vector2(650, 0) + half_army + Vector2.ONE * 80.0
	var base_min := _center + Vector2(-1050, -280)
	var base_max := _center + Vector2(-350, 280)
	if army_min.x <= base_max.x or army_min.x < 0 or army_min.y < 0 or army_max.x > playable.x or army_max.y > playable.y or base_min.x < 0 or base_min.y < 0 or base_max.y > playable.y:
		return "The map is too small to reserve separate areas for the benchmark base and 20,000 CP army."
	var acquire: Dictionary = game._world.debug_set_auto_acquire(false)
	if not acquire.get("ok", false): return "Could not disable combat for the idle benchmark."
	var fog: Dictionary = game._world.execute_lua("local p=bfme.players[%d]; p:setFogOfWar(false); p.commandPointsBase=20000; p.commandPointsCap=20000" % _player)
	if not fog.get("ok", false): return "Could not configure benchmark player: %s" % fog.get("output", "")
	game._world.update_lua()
	for index in _preset.buildings.size():
		if cancelled: return "Cancelled."
		var offset := Vector2(float(index % 4) - 1.5, float(index / 4) - 1.0) * 180.0
		var pos := _center + offset + Vector2(-700.0, 0)
		var id: int = game._world.create_object(_preset.buildings[index], _player, pos.x, pos.y, 0.0)
		if id < 0: return "Could not create benchmark building %s." % _preset.buildings[index]
		await get_tree().process_frame
	_scene = Node3D.new()
	add_child(_scene)
	_camera = Camera3D.new()
	_camera.far = 60000.0
	_camera.near = 2.0
	_scene.add_child(_camera)
	_hud = ClassDB.instantiate("InGameHudNode")
	add_child(_hud)
	var setup: Dictionary = _hud.setup(game._fs, game._world, _camera, "Player_1", {"camera_start": Vector3(_center.x, 0, -_center.y), "free_camera": true, "edge_scroll": "off"})
	if not setup.get("ok", false): return "Benchmark HUD setup failed: %s" % setup.get("errors", [])
	_environment_before = game._env.environment
	game._env.environment = _environment_before.duplicate()
	if loaded.has("lighting"):
		game._env.environment.ambient_light_color = loaded.lighting.ambient
		game._env.environment.ambient_light_energy = 1.0
		var sun := DirectionalLight3D.new()
		sun.light_color = loaded.lighting.diffuse
		sun.shadow_enabled = int(game._shell.get_detail_state().get("shadow_lod", 3)) > 0
		sun.directional_shadow_max_distance = 3000.0
		_scene.add_child(sun)
		var dir: Vector3 = loaded.lighting.direction
		if not dir.is_zero_approx(): sun.basis = Basis.looking_at(dir.normalized(), Vector3.UP if absf(dir.normalized().y) < 0.99 else Vector3.RIGHT)
	if is_instance_valid(game._sun):
		_sun_was_visible = game._sun.visible
		game._sun.hide()
	game._game_options.game_started(game._world, _hud)
	_hud.set_input_enabled(false)
	_hud.set_free_camera(true)
	_camera.make_current()
	return ""

func _used_cp() -> int:
	for player in game._world.get_economy().get("players", []):
		if int(player.index) == _player: return int(player.cp_used)
	return -1

func _populate(target: int) -> String:
	var current := _used_cp()
	if current < 0 or current > target: return "Unexpected benchmark CP before spawning: %d." % current
	var plan: Dictionary = Plan.army(target - current, _preset.unit_costs)
	if not plan.ok: return plan.error
	_columns = ceili(sqrt(float(plan.units.size())))
	_army_origin = _center + Vector2(650, 0) - Vector2.ONE * float(_columns - 1) * _spacing * 0.5
	_status.text = "Preparing %s CP · %d units to add…" % [target, plan.units.size()]
	for index in plan.units.size():
		if cancelled: return "Cancelled."
		var pos := _army_origin + Vector2(float(_spawned % _columns), float(_spawned / _columns)) * _spacing
		var name: String = plan.units[index]
		var id: int = game._world.create_object(name, _player, pos.x, pos.y, 0.0)
		if id < 0: return "Could not create benchmark unit %s." % name
		_unit_ids.append(id)
		_spawned += 1
		_roster[name] = int(_roster.get(name, 0)) + 1
		if index % 24 == 0:
			_status.text = "Preparing %s CP · %d units created…" % [target, _spawned]
			await get_tree().process_frame
	# Mutation queues are bounded; protect detached ID batches outside capture.
	for begin in range(0, _unit_ids.size(), 64):
		var ids := PackedStringArray()
		for index in range(begin, mini(begin + 64, _unit_ids.size())): ids.append(str(_unit_ids[index]))
		var protect: Dictionary = game._world.execute_lua("for _,id in ipairs({%s}) do local u=bfme.unitInstances[id]; if u then u:setInvulnerable(true) end end" % ",".join(ids))
		if not protect.get("ok", false): return "Could not protect the benchmark army: %s" % protect.get("output", "")
		game._world.update_lua()
		if cancelled: return "Cancelled."
		await get_tree().process_frame
	if _used_cp() != target: return "CP verification failed: requested %d, created %d." % [target, _used_cp()]
	# Bring the relevant army area into view. The larger case uses a wider
	# camera, recorded in the report rather than pretending both views match.
	var height := 2200.0 if target == int(_preset.normal_cp) else 3200.0
	var aim := _center + Vector2(-100, 0) if target == int(_preset.normal_cp) else _center + Vector2(250, 0)
	_hud.camera_look_at(aim)
	_hud.camera_set_height(height)
	report["camera_%d_cp" % target] = _hud.get_camera()
	game._world.set_paused(false)
	game._world.set_logic_thread(_old_logic_thread)
	game._world.set_auto_advance(true)
	return ""

func _clear_world() -> String:
	var error := ""
	if _world_loaded:
		game._world.set_auto_advance(false)
		game._world.set_logic_thread(false)
		if is_instance_valid(_hud):
			_hud.free()
			_hud = null
		game._game_options.game_ended()
		if _environment_before != null: game._env.environment = _environment_before
		if _environment_before != null and is_instance_valid(game._sun): game._sun.visible = _sun_was_visible
		var cleared: Dictionary = game._world.clear_game_data(false)
		if not cleared.get("ok", false): error = "Cleanup failed: %s" % cleared
		_world_loaded = false
	if is_instance_valid(_scene): _scene.free()
	_camera = null
	_scene = null
	_environment_before = null
	_spawned = 0
	_roster.clear()
	_unit_ids.clear()
	_player = -1
	return error

func _finish(error: String) -> void:
	if not running or _finishing: return
	_finishing = true
	var script_failed: bool = _logger != null and _logger.script_errors() > 0
	if _logger != null:
		OS.remove_logger(_logger)
		_logger = null
	if is_instance_valid(_sampler):
		_sampler._set_enabled(false)
		_sampler.queue_free()
	var clear_error := _clear_world()
	if not clear_error.is_empty(): error += " " + clear_error
	game._world.set_logic_thread(_old_logic_thread)
	if _game_suspended:
		game._world.visible = _old_world_visible
		game._camera.current = _old_camera
		game._shell.visible = _old_shell_visible
		game._shell.auto_process = _old_shell_process
		game.set_process(_old_process)
		game._shell_back_from_game()
		_game_suspended = false
	# Restore the main menu without score screens, game records or replays.
	if not await _main_menu(): error += " Could not restore the main menu."
	if _caps_changed:
		Engine.max_fps = _old_max_fps
		DisplayServer.window_set_vsync_mode(_old_vsync)
		_caps_changed = false
	report["status"] = "failed" if script_failed else ("cancelled" if cancelled else ("complete" if error.is_empty() else "failed"))
	report["error"] = error
	if report.status == "complete" and report.stages.size() != 4:
		report.status = "failed"
		report.error = "The benchmark did not record all four scenarios."
	_save_report()
	running = false
	_progress.hide()
	_panel.show()
	_shade.show()
	_show_results()
	_close.grab_focus()

func _save_report() -> void:
	var error := DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("user://benchmarks"))
	if error != OK:
		report["export_error"] = "Could not create reports folder: %s" % error
		return
	_path = "user://benchmarks/settings-%d-%d.json" % [int(Time.get_unix_time_from_system()), Time.get_ticks_usec()]
	for path in [_path, LAST_REPORT]:
		var file := FileAccess.open(path, FileAccess.WRITE)
		if file == null:
			report["export_error"] = "Could not write benchmark report: %s" % FileAccess.get_open_error()
			return
		file.store_string(JSON.stringify(report, "\t"))

func _show_results() -> void:
	_results.text = "Benchmark: %s\n" % report.get("status", "")
	for stage in report.get("stages", []):
		var summary: Dictionary = stage.frames_summary
		var gpu: Dictionary = stage.get("summary", {}).get("render_gpu", {})
		_results.text += "%s: %.1f FPS · p95 %.2f ms · p99 %.2f ms · GPU %s\n" % [str(stage.stage).replace("_", " ").capitalize(), summary.fps,
			summary.frame_p95_ms, summary.frame_p99_ms, "%.2f ms" % gpu.mean_ms if gpu.has("mean_ms") else "n/a"]
	if not str(report.get("error", "")).is_empty(): _results.text += str(report.error) + "\n"
	if report.has("export_error"): _results.text += str(report.export_error) + "\n"
	if not _path.is_empty(): _results.text += ProjectSettings.globalize_path(_path)
	_spawned = 0
	_roster.clear()
	_unit_ids.clear()
	_player = -1

func _exit_tree() -> void:
	if _logger != null: OS.remove_logger(_logger)
	# A running tool normally closes only through _finish. Application shutdown
	# still restores temporary display caps before its nodes are released.
	if _caps_changed:
		Engine.max_fps = _old_max_fps
		DisplayServer.window_set_vsync_mode(_old_vsync)

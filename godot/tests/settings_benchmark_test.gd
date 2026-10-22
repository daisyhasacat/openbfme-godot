## Installed-data integration. Uses short captures but the full 20,000 CP scene.
## Headless runs verify orchestration only; native runs exercise GPU timings.
extends SceneTree

var _probe_held := false
var _hold_frames := 0

var _logger = preload("res://scripts/walk_error_logger.gd").new()

func _initialize() -> void:
	OS.add_logger(_logger)
	process_frame.connect(func():
		if _logger.script_errors() > 0:
			printerr("SETTINGS BENCHMARK SCRIPT FAILURE: ", _logger.first_error())
			quit(1))
	call_deferred("_run")

func _run() -> void:
	get_tree_watchdog()
	var game = load("res://scripts/game.gd").new()
	game._intro = "off"
	game._options_file = OS.get_user_data_dir().path_join("benchmark-test-options.ini") if OS.get_name() == "iOS" else "/tmp/openbfme-benchmark-test-options.ini"
	root.add_child(game)
	while game._state not in [game.State.MENU, game.State.FAILED] or game._display_screen == null:
		await process_frame
	assert(game._state == game.State.MENU)
	await create_timer(1.0).timeout
	var profile := OS.get_environment("BENCHMARK_SHADOW_PROFILE")
	if not profile.is_empty():
		assert(profile in ["auto", "balanced", "high"])
		assert(game._shell.set_option("OpenBFMEShadowProfile", profile))
	var scale := OS.get_environment("BENCHMARK_RENDER_SCALE")
	if not scale.is_empty():
		game._display.render_scale = float(scale)
		game._display.apply(root)
	var tool = await open_tool(game)
	var environment_before = game._env.environment
	var sun_before = game._sun
	var camera_transform: Transform3D = game._camera.transform
	var cap := Engine.max_fps
	var vsync := DisplayServer.window_get_vsync_mode()
	# Cancel during warmup; no match is loaded and display caps come back.
	tool.start()
	tool.cancelled = true
	while tool.running: await process_frame
	assert(tool.report.status == "cancelled")
	assert(Engine.max_fps == cap and DisplayServer.window_get_vsync_mode() == vsync)
	assert(game._world.get_object_count() == 0)
	# Run from the actual Settings > Benchmark button, not a direct signal.
	tool.free()
	tool = await open_tool(game)
	var hold_probe := func():
		if game._state != game.State.PLAYING or not is_instance_valid(game._load_metrics): return
		if not _probe_held:
			_probe_held = true
			_hold_frames = 3
			game._hud.hold_input(true)
			var pending: int = game._hud.get_state().keys.pending
			var key := InputEventKey.new()
			key.keycode = KEY_F1
			key.pressed = true
			root.push_input(key)
			var click := InputEventMouseButton.new()
			click.button_index = MOUSE_BUTTON_LEFT
			click.pressed = true
			root.push_input(click)
			var unrelated := InputEventAction.new()
			unrelated.action = "benchmark_unrelated_action"
			root.push_input(unrelated)
			assert(game._hud.get_state().keys.pending == pending, "Real input must not enter the diagnostic match")
		if _hold_frames > 0:
			assert(game._hud.get_startup_probe_usec() == 0, "A held input queue must not be reported as responsive")
			_hold_frames -= 1
			if _hold_frames == 0: game._hud.hold_input(false)
	process_frame.connect(hold_probe)
	tool.start()
	while tool.running:
		await process_frame
	process_frame.disconnect(hold_probe)
	assert(tool.report.status == "complete", str(tool.report.get("error", "")))
	assert(tool.report.match_startup.status == "complete")
	var startup: Dictionary = tool.report.match_startup
	assert(startup.pipeline_compilations.available == (DisplayServer.get_name() != "headless"))
	var milestone_names: Array = []
	for milestone in startup.pipeline_compilations.milestones:
		milestone_names.append(milestone.phase)
		assert(milestone.counts.keys() == ["canvas", "mesh", "surface", "draw", "specialization"])
	assert(milestone_names.has("hud_started") and milestone_names.has("hud_complete"))
	if DisplayServer.get_name() != "headless": assert(milestone_names[-1] == "observation_complete")
	var effective: String = preload("res://scripts/shadow_profile.gd").resolved(game._game_options.current().OpenBFMEShadowProfile)
	assert(tool.report.shadow_profile.effective == effective)
	assert(tool.report.shadow_profile.cascades == (2 if effective == "balanced" else 4))
	assert(tool.report.shadow_profile.blend_splits == (effective == "high"))
	assert(startup.phases_ms.has("prepare_complete") and startup.phases_ms.has("hud_complete") and startup.phases_ms.has("playing"))
	assert(startup.map_load_ms == startup.phases_ms.map_load_complete - startup.phases_ms.map_load_started)
	if DisplayServer.get_name() != "headless":
		assert(_probe_held and _hold_frames == 0)
		assert(startup.render_available)
		# tolerance: wall-clock measurements vary by device; only causal ordering is required.
		assert(startup.world_frame_to_controls_ms >= 0 and startup.input_queue_ms >= 0)
		assert(not startup.post_reveal_frame_gaps_ms.is_empty())
		assert(startup.post_reveal_max_frame_gap_ms == startup.post_reveal_frame_gaps_ms.max())
	assert(tool.report.stages.size() == 4)
	assert(tool.report.stages[0].stage == "main_menu")
	assert(tool.report.stages[1].stage == "lobby")
	assert(tool.report.stages[0].shell_stack == PackedStringArray(["MainMenu.apt"]))
	assert(tool.report.stages[1].shell_stack[-1] == "Skirmish.apt")
	assert(tool.report.stages[2].workload.cp == 1000)
	assert(tool.report.stages[3].workload.cp == 20000)
	for stage in tool.report.stages:
		assert(not stage.samples.is_empty())
		assert(stage.pipeline_compilations.capture_delta.keys() == ["canvas", "mesh", "surface", "draw", "specialization"])
		if stage.workload.has("cp"):
			var motion: Dictionary = stage.workload.camera_motion
			assert(motion.path == "diagonal_pan_max_to_min_zoom")
			assert(motion.start.height_above_ground == motion.max_height)
			assert(motion.end.height_above_ground == motion.min_height)
			assert(motion.trace[0].height == motion.max_height)
			assert(motion.trace[-1].height == motion.min_height)
			assert(motion.trace[-1].position - motion.trace[0].position == Vector2(1200, 400))
			var cp := 0
			for cost in tool.report.preset.unit_costs:
				cp += int(cost.cp) * int(stage.workload.roster.get(cost.name, 0))
			assert(cp == int(stage.workload.cp))
			assert(int(stage.workload.cp_after) == int(stage.workload.cp))
	assert(Engine.max_fps == cap and DisplayServer.window_get_vsync_mode() == vsync)
	assert(game.is_processing())
	assert(game._env.environment == environment_before and game._sun == sun_before)
	assert(game._camera.transform == camera_transform)
	assert(game._shell.shell_stack() == PackedStringArray(["MainMenu.apt"]))
	assert(game._world.get_object_count() == 0)
	assert(not tool.report.has("export_error"))
	var exported = JSON.parse_string(FileAccess.get_file_as_string(tool._path))
	assert(exported.status == "complete" and exported.stages.size() == 4)
	if DisplayServer.get_name() != "headless" and not OS.get_environment("BENCHMARK_SHOTS").is_empty():
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png(OS.get_environment("BENCHMARK_SHOTS") + "-results.png")
	var complete_path: String = tool._path
	tool.free()
	tool = await open_tool(game)
	tool.start()
	while tool.running and not tool._world_loaded: await process_frame
	tool.cancelled = true
	while tool.running: await process_frame
	assert(tool.report.status == "cancelled")
	assert(game._world.get_object_count() == 0 and game.is_processing())
	assert(game._shell.shell_stack() == PackedStringArray(["MainMenu.apt"]))
	assert(Engine.max_fps == cap and DisplayServer.window_get_vsync_mode() == vsync)
	tool.free()
	tool = await open_tool(game)
	tool.start()
	var cancel_phase := "prepare_complete" if DisplayServer.get_name() == "headless" else "playing"
	while tool.running and (not is_instance_valid(game._load_metrics) or not game._load_metrics.phases.has(cancel_phase)):
		await process_frame
	tool.cancelled = true
	while tool.running: await process_frame
	assert(tool.report.status == "cancelled")
	assert(not is_instance_valid(game._load_metrics))
	assert(game._env.environment == environment_before and game._sun == sun_before)
	assert(game._camera.transform == camera_transform)
	assert(game._world.get_object_count() == 0 and game.is_processing())
	assert(game._shell.shell_stack() == PackedStringArray(["MainMenu.apt"]))
	assert(Engine.max_fps == cap and DisplayServer.window_get_vsync_mode() == vsync)
	print("SETTINGS BENCHMARK PASS: ", complete_path)
	quit()

func open_tool(game):
	assert(game._shell.open_options(false, false, false, false))
	while not game._display_screen.is_open(): await process_frame
	game._display_screen._show_tab("Graphics")
	var shadow_rows: Array = game._display_screen._spec().rows.filter(func(row): return row.get("key", "") == "OpenBFMEShadowProfile")
	assert(shadow_rows.size() == 1)
	assert(shadow_rows[0].values == ["auto", "balanced", "high"])
	game._display_screen._show_tab("Benchmark")
	assert(game._display_screen._mods_button.visible)
	game._display_screen._mods_button.pressed.emit()
	var tool = game._benchmark_tool
	assert(is_instance_valid(tool))
	tool.allow_headless = true
	tool.warmup_seconds = 3.0 if OS.get_environment("BENCHMARK_FULL") == "1" else 0.1
	tool._duration.min_value = 0.15
	tool._duration.step = 0.01
	tool._duration.value = 3.0 if OS.get_environment("BENCHMARK_FULL") == "1" else 0.15
	if DisplayServer.get_name() != "headless" and not OS.get_environment("BENCHMARK_SHOTS").is_empty() and not FileAccess.file_exists(OS.get_environment("BENCHMARK_SHOTS") + "-setup.png"):
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png(OS.get_environment("BENCHMARK_SHOTS") + "-setup.png")
	return tool

func get_tree_watchdog() -> void:
	create_timer(360.0).timeout.connect(func():
		printerr("SETTINGS BENCHMARK FAIL: watchdog expired")
		quit(1))

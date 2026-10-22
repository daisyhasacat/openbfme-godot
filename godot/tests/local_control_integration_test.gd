## Graphical installed-data exercise of the actual local diagnostic controller.
extends SceneTree
var _logger = preload("res://scripts/walk_error_logger.gd").new()
func _initialize() -> void:
	OS.add_logger(_logger)
	process_frame.connect(func():
		if _logger.script_errors() > 0: quit(1))
	create_timer(240.0).timeout.connect(func(): quit(1))
	call_deferred("_run")
func _run() -> void:
	var game = load("res://scripts/game.gd").new()
	game._intro = "off"
	game._options_file = "/tmp/openbfme-local-control-integration.ini"
	root.add_child(game)
	while game._state != game.State.MENU or not is_instance_valid(game._local_control): await process_frame
	var control = game._local_control
	control.directory = "user://local-control-integration-" + str(Time.get_ticks_usec())
	control._ready()
	var original_profile = game._shell.get_option("OpenBFMEShadowProfile")
	var original_scale := root.scaling_3d_scale
	var original_cap := Engine.max_fps
	var original_vsync := DisplayServer.window_get_vsync_mode()
	assert(control.execute({"version": 1, "id": "graphics", "command": "settings", "tab": "Graphics"}).status == "ok")
	assert(game._display_screen.current_tab() == "Graphics")
	assert(control.execute({"version": 1, "id": "run", "command": "benchmark_start", "seconds": 3,
		"shadow_profile": "balanced", "render_scale": 0.75}).status == "accepted")
	game._benchmark_tool.warmup_seconds = 0.1
	while game._benchmark_tool.running: await process_frame
	await process_frame # controller publishes completion and restores overrides.
	var job = JSON.parse_string(FileAccess.get_file_as_string(control.directory.path_join("job.json")))
	assert(job.id == "run" and job.status == "complete", str(job))
	assert(game._benchmark_tool.report.shadow_profile.effective == "balanced")
	assert(game._benchmark_tool.report.render_scale == 0.75)
	assert(game._shell.get_option("OpenBFMEShadowProfile") == original_profile)
	assert(root.scaling_3d_scale == original_scale)
	assert(Engine.max_fps == original_cap and DisplayServer.window_get_vsync_mode() == original_vsync)
	assert(control.execute({"version": 1, "id": "cancel-run", "command": "benchmark_start", "seconds": 3,
		"shadow_profile": "high", "render_scale": 0.5}).status == "accepted")
	assert(control.execute({"version": 1, "id": "cancel", "command": "benchmark_cancel"}).status == "ok")
	while game._benchmark_tool.running: await process_frame
	await process_frame
	job = JSON.parse_string(FileAccess.get_file_as_string(control.directory.path_join("job.json")))
	assert(job.id == "cancel-run" and job.status == "cancelled")
	assert(game._shell.get_option("OpenBFMEShadowProfile") == original_profile)
	assert(root.scaling_3d_scale == original_scale)
	assert(Engine.max_fps == original_cap and DisplayServer.window_get_vsync_mode() == original_vsync)
	assert(control.execute({"version": 1, "id": "capture", "command": "screenshot"}).status == "accepted")
	await RenderingServer.frame_post_draw
	await process_frame
	assert(FileAccess.file_exists(control.directory.path_join("capture.png")))
	assert(control.execute({"version": 1, "id": "run", "command": "benchmark_start"}).status == "duplicate")
	print("LOCAL CONTROL INTEGRATION PASS: ", control.directory)
	quit()

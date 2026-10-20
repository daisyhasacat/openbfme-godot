extends SceneTree
const PerfPanel = preload("res://scripts/console_performance.gd")
class FakeHud extends Node:
	func get_frame_timings() -> Dictionary:
		return {"hud_update_ms": 12.5, "hud_windows_ms": 8.0, "hud_radar_ms": 2.0}
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var console = preload("res://scripts/lua_console.lua").new()
	console.history_path = "/tmp/performance-console-history.cfg"
	console.controller_options_path = "/tmp/performance-controller.cfg"
	console.execute = func(code: String) -> Dictionary: return world.execute_lua(code)
	root.add_child(console)
	var panel = PerfPanel.new()
	panel.world = world
	var hud := FakeHud.new()
	root.add_child(hud)
	panel.hud = func() -> Node: return hud
	console.add_performance_tab(panel)
	assert(console._tabs.get_tab_count() == 6)
	assert(console._tabs.get_tab_title(5) == "Performance")
	var summary := PerfPanel.summarize([{"frame": 1.0}, {"frame": 2.0}, {"frame": 6.0}])
	assert(summary.frame == {"mean_ms": 3.0, "p95_ms": 6.0, "max_ms": 6.0, "samples": 3})
	assert(not summary.has("render_gpu"))
	assert(PerfPanel.summarize([]).is_empty())
	# A capture must keep sampling even when its console is closed.
	panel.start_capture(1)
	assert(not console.is_open())
	for i in 12:
		world.execute_lua("return 42")
		await process_frame
	await create_timer(1.1).timeout
	await process_frame
	assert(not panel.capturing)
	# tolerance: scheduling determines frame count; a capture must have at least one complete sample.
	assert(panel.last_report.frames > 0)
	assert(panel.last_report.summary.has("lua"))
	assert(panel.last_report.summary.hud_update_ms.mean_ms == 12.5)
	assert(panel.last_report.summary.hud_windows_ms.mean_ms == 8.0)
	assert(panel.last_report.summary.hud_radar_ms.mean_ms == 2.0)
	assert(panel.last_report.summary.has("godot_process"))
	assert(panel.last_report.summary.has("scene_callbacks_ms"))
	assert(panel.last_report.summary.worker_wait.max_ms == 0.0)
	for metric in ["presentation_events", "drawable_animation", "drawable_transforms", "client_effects"]:
		assert(panel.last_report.summary.has(metric))
	if DisplayServer.get_name() == "headless": assert(not panel.last_report.summary.has("render_gpu"))
	var report_copy: Dictionary = panel.last_report.duplicate(true)
	panel._live.clear()
	assert(panel.last_report == report_copy)
	panel.export_report()
	assert(panel._status.text.begins_with("Exported "))
	var path: String = panel._status.text.trim_prefix("Exported ")
	var loaded = JSON.parse_string(FileAccess.get_file_as_string(path))
	assert(loaded.frames == panel.last_report.frames)
	assert(JSON.stringify(loaded.summary.frame.mean_ms) == JSON.stringify(panel.last_report.summary.frame.mean_ms))
	DirAccess.remove_absolute(path)
	if not OS.get_environment("PERF_SCREENSHOT").is_empty():
		root.size = Vector2i(1600, 1100)
		console.toggle()
		console._tabs.current_tab = 5
		await process_frame
		await process_frame
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png(OS.get_environment("PERF_SCREENSHOT"))
	hud.free()
	console.free()
	world.free()
	print("CONSOLE PERFORMANCE PASS")
	quit()

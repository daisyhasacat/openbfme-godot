# Local diagnostics only. Sampling never hashes or locks the live simulation.
extends VBoxContainer

var world: Node
var hud: Callable
var capturing := false
var capture_seconds := 10.0
var samples: Array[Dictionary] = []
var last_report: Dictionary = {}
var _live: Array[Dictionary] = []
var _previous: Dictionary = {}
var _last_us := 0
var _callbacks_begin_us := 0
var _previous_wait_ms := 0.0
var _elapsed := 0.0
var _refresh := 0.0
var _enabled := false
var _show_capture := false
var _status: Label
var _workload: Label
var _table: Tree
var _duration: SpinBox
var _capture: Button
var _export: Button
var _live_toggle: CheckBox
var _rows: Dictionary = {}
const LIVE_FRAMES := 240
const MAX_CAPTURE_FRAMES := 36000
const METRICS := [
	["frame", "Frame interval", "Wall time between displayed frames, including waiting / VSync."],
	["simulation", "Simulation · all phases", "Completed simulation work on main / logic worker threads."],
	["lua", "Lua · gameplay / console", "Includes snapshots and mutations; may overlap simulation / pathing."],
	["pathing", "Pathfinding / group orders", "Path searches, queue, patches and group destination planning; subset of simulation or commands."],
	["physics", "Unit physics updates", "PhysicsBehavior updates; subset of simulation."],
	["collision", "Unit collision", "AI collision broadphase and contact processing; subset of simulation."],
	["advance_ms", "Main · game advance total", "Entire GameWorld advance, including simulation, presentation, effects and render synchronization. This is not worker wait time."],
	["worker_wait", "Main · actual worker wait", "Time blocked waiting for the logic worker since the previous sample; includes waits from scripts and input outside advance."],
	["presentation_events", "  Presentation events", "Applying completed simulation events and hero statistics; part of game advance."],
	["drawable_animation", "  Drawable animations", "Client animation state updates; part of game advance."],
	["drawable_transforms", "  Drawable transforms", "Snapshot interpolation, transform synchronization and animation sounds; part of game advance."],
	["client_effects", "  Client effects / audio", "Attached effects, ambient sounds, buffs, wall paving and tree events; part of game advance."],
	["sync_ms", "Main · render synchronization", "Client drawable / visibility synchronization; includes rows below."],
	["shroud_ms", "  Shroud", "Subset of render synchronization."],
	["views_ms", "  Drawable views", "Subset of render synchronization."],
	["fx_ms", "  Effects", "Subset of render synchronization."],
	["dyn_pose_ms", "Animation poses", "Dynamic instancer pose evaluation; includes pose worker wait."],
	["dyn_upload_ms", "Instance uploads", "Dynamic instancer transform / buffer upload submission."],
	["dyn_mapper_ms", "Dynamic texture animation", "Dynamic instancer texture mapper updates."],
	["static_mapper_ms", "Static texture animation", "Static instancer texture mapper updates."],
	["hud_update_ms", "HUD · total update", "HUD update outside GameWorld.advance; includes the HUD rows below."],
	["hud_input_ms", "  HUD input / picking", "Subset of HUD update."],
	["hud_bar_ms", "  HUD command bar", "Subset of HUD update."],
	["hud_windows_ms", "  HUD windows / movies", "Subset of HUD update."],
	["hud_palantir_ms", "  HUD Palantir", "Subset of HUD update."],
	["hud_radar_ms", "  HUD minimap", "Subset of HUD update."],
	["hud_icons_ms", "  HUD icon submission", "Subset of HUD update."],
	["hud_decals_ms", "  HUD world decals", "Subset of HUD update."],
	["scene_callbacks_ms", "Scene · process callbacks", "Wall time from SceneTree.process_frame to this late-priority sampler; excludes subsequent RenderingServer sync/draw and the sampler itself."],
	["godot_process", "Godot · frame processing peak", "Godot TIME_PROCESS is the peak over its sampling interval, including SceneTree processing, RenderingServer sync and drawing. It is not a per-frame callback measurement."],
	["render_cpu", "Viewport rendering · CPU", "RenderingServer viewport CPU timing; overlaps main work."],
	["render_gpu", "Viewport rendering · GPU", "Asynchronous viewport GPU timing. Unavailable with the headless renderer."],
	["godot_physics", "Godot physics", "Godot physics processing, separate from BFME unit physics."],
	["main_cpu_ms", "Main thread · CPU", "CPU running time for the last frame, excludes time waiting."],
]

func _ready() -> void:
	name = "Performance"
	process_priority = 10000
	get_tree().process_frame.connect(_begin_callbacks)
	add_theme_constant_override("separation", 8)
	var toolbar := HFlowContainer.new()
	add_child(toolbar)
	_live_toggle = CheckBox.new()
	_live_toggle.text = "Live timings"
	_live_toggle.button_pressed = true
	toolbar.add_child(_live_toggle)
	var live_button := Button.new()
	live_button.text = "Show live"
	live_button.pressed.connect(func():
		_show_capture = false
		_live_toggle.button_pressed = true
		_status.text = "Live: last 240 frames · times in milliseconds per display frame")
	toolbar.add_child(live_button)
	_duration = SpinBox.new()
	_duration.min_value = 1
	_duration.max_value = 120
	_duration.value = 10
	_duration.suffix = "s"
	toolbar.add_child(_duration)
	_capture = Button.new()
	_capture.text = "Start benchmark"
	_capture.pressed.connect(func():
		if capturing: finish_capture()
		else: start_capture(_duration.value))
	toolbar.add_child(_capture)
	_export = Button.new()
	_export.text = "Export JSON"
	_export.disabled = true
	_export.pressed.connect(export_report)
	toolbar.add_child(_export)
	_status = Label.new()
	_status.text = "Live: last 240 frames · times in milliseconds per display frame"
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(_status)
	_workload = Label.new()
	_workload.text = "Waiting for samples…"
	add_child(_workload)
	_table = Tree.new()
	_table.columns = 4
	_table.hide_root = true
	_table.column_titles_visible = true
	_table.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_table.custom_minimum_size = Vector2(0, 210)
	for i in 4:
		_table.set_column_title(i, ["Work / scope", "Average ms", "p95 ms", "Worst ms"][i])
		_table.set_column_expand(i, i == 0)
		if i != 0: _table.set_column_custom_minimum_width(i, 90)
	add_child(_table)
	var tree_root := _table.create_item()
	for metric in METRICS:
		var row := _table.create_item(tree_root)
		row.set_text(0, metric[1])
		for i in 4: row.set_tooltip_text(i, metric[2])
		_rows[metric[0]] = row
	var note := Label.new()
	note.text = "Scopes overlap; do not add rows. Worker work is charged when a scope completes.\nBenchmark continues with the console closed. GPU timings may lag; headless rendering is unavailable."
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(note)

func _set_enabled(value: bool) -> void:
	if _enabled == value: return
	_enabled = value
	if is_instance_valid(world): world.set_performance_enabled(value)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), value)
	_previous = {}
	_last_us = 0
	_callbacks_begin_us = 0

func _begin_callbacks() -> void:
	if _enabled: _callbacks_begin_us = Time.get_ticks_usec()

func _process(_delta: float) -> void:
	var callbacks_end_us := Time.get_ticks_usec()
	_set_enabled(is_instance_valid(world) and (capturing or (is_visible_in_tree() and _live_toggle.button_pressed)))
	if not _enabled: return
	var now := Time.get_ticks_usec()
	var counters: Dictionary = world.get_performance_counters()
	if _last_us == 0:
		_last_us = now
		_previous = counters
		_previous_wait_ms = float(world.get_frame_timings().get("logic_wait_ms",0.0))
		return
	var interval := float(now - _last_us) / 1000.0
	_last_us = now
	var sample: Dictionary = world.get_frame_timings()
	if _callbacks_begin_us != 0:
		sample["scene_callbacks_ms"] = float(callbacks_end_us - _callbacks_begin_us) / 1000.0
	var live_hud: Node = hud.call() if hud.is_valid() else null
	if is_instance_valid(live_hud): sample.merge(live_hud.get_frame_timings())
	var wait_total := float(sample.get("logic_wait_ms",0.0))
	sample["worker_wait"] = maxf(0.0,wait_total-_previous_wait_ms)
	_previous_wait_ms = wait_total
	sample["frame"] = interval
	sample["calls"] = {}
	for key in counters:
		sample[key] = maxf(0, float(counters[key].ms) - float(_previous[key].ms))
		sample.calls[key] = maxi(0, int(counters[key].calls) - int(_previous[key].calls))
	_previous = counters
	if DisplayServer.get_name() != "headless":
		var rid := get_viewport().get_viewport_rid()
		var cpu := RenderingServer.viewport_get_measured_render_time_cpu(rid)
		var gpu := RenderingServer.viewport_get_measured_render_time_gpu(rid)
		if cpu > 0: sample["render_cpu"] = cpu
		if gpu > 0: sample["render_gpu"] = gpu
	sample["godot_process"] = Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0
	sample["godot_physics"] = Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000.0
	sample["draw_calls"] = int(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME))
	_live.append(sample)
	if _live.size() > LIVE_FRAMES: _live.pop_front()
	if capturing:
		samples.append(sample)
		_elapsed += interval / 1000.0
		_status.text = "Recording %.1f / %.0f s · %d frames · close console to benchmark gameplay" % [_elapsed, capture_seconds, samples.size()]
		if _elapsed >= capture_seconds or samples.size() == MAX_CAPTURE_FRAMES: finish_capture()
	_refresh += interval
	if _refresh >= 250:
		_refresh = 0
		var mean_interval := float(summarize(_live).get("frame", {}).get("mean_ms", 0))
		_workload.text = "%.1f FPS · %d presented objects · %d draw calls · %d primitives" % [1000.0 / mean_interval if mean_interval > 0 else 0, int(sample.get("presented_objects", 0)), int(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)), int(Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME))]
		if not _show_capture: _show_summary(summarize(_live))

# Statistics use nearest-rank p95; unavailable metrics remain absent, never fake zero.
static func summarize(frames: Array[Dictionary]) -> Dictionary:
	var result := {}
	for metric in METRICS:
		var values: Array[float] = []
		var total := 0.0
		for frame in frames:
			if frame.has(metric[0]):
				values.append(float(frame[metric[0]]))
				total += float(frame[metric[0]])
		if values.is_empty(): continue
		values.sort()
		result[metric[0]] = {"mean_ms": total / values.size(), "p95_ms": values[ceili(values.size() * 0.95) - 1], "max_ms": values.back(), "samples": values.size()}
	return result

func _show_summary(summary: Dictionary) -> void:
	for key in _rows:
		var row: TreeItem = _rows[key]
		for column in range(1, 4):
			row.set_text(column, "%.2f" % summary[key][["mean_ms", "p95_ms", "max_ms"][column - 1]] if summary.has(key) else "n/a")

func start_capture(seconds: float = 10) -> void:
	capture_seconds = clampf(seconds, 1, 120)
	samples.clear()
	last_report.clear()
	_live.clear()
	_elapsed = 0
	_previous = {}
	_last_us = 0
	capturing = true
	_show_capture = false
	_capture.text = "Stop benchmark"
	_export.disabled = true

func finish_capture() -> void:
	capturing = false
	_show_capture = true
	_capture.text = "Start benchmark"
	last_report = {"version": 1, "created": Time.get_datetime_string_from_system(), "seconds": _elapsed, "frames": samples.size(), "display_server": DisplayServer.get_name(), "renderer": RenderingServer.get_current_rendering_method(), "driver": RenderingServer.get_current_rendering_driver_name(), "build": ClassDB.instantiate("ReleaseInfo").version_line(), "metrics": METRICS, "summary": summarize(samples), "samples": samples.duplicate(true), "note": "Milliseconds per display sample. Nested and parallel scopes overlap; never sum rows."}
	_export.disabled = samples.is_empty()
	_status.text = "Benchmark complete · %.1f s · %d frames · results held until next capture" % [_elapsed, samples.size()]
	_show_summary(last_report.summary)

func export_report() -> void:
	if last_report.is_empty(): return
	var directory := "user://benchmarks"
	var error := DirAccess.make_dir_recursive_absolute(directory)
	if error != OK:
		_status.text = "Could not create benchmark folder: %s" % error
		return
	var path := directory.path_join("performance-%d-%d.json" % [int(Time.get_unix_time_from_system()), Time.get_ticks_usec()])
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		_status.text = "Could not export benchmark: %s" % FileAccess.get_open_error()
		return
	file.store_string(JSON.stringify(last_report, "\t"))
	_status.text = "Exported " + ProjectSettings.globalize_path(path)

func _exit_tree() -> void:
	_set_enabled(false)

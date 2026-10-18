## Rendered transport proof, launched by game.gd --replay=... --caster-check=<capture directory>.
extends RefCounted

var failures := 0
var host: Node
var directory := ""


func check(ok: bool, what: String) -> void:
	print("CASTER UI %s: %s" % ["PASS" if ok else "FAIL", what])
	if not ok:
		failures += 1


func frames(count: int = 3) -> void:
	for i in count:
		await host.get_tree().process_frame


func key(code: int) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.pressed = true
	Input.parse_input_event(event)
	await frames(1)
	event = InputEventKey.new()
	event.keycode = code
	event.pressed = false
	Input.parse_input_event(event)
	await frames()


func click(at: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = at
	motion.global_position = at
	Input.parse_input_event(motion)
	await frames(1)
	var event := InputEventMouseButton.new()
	event.position = at
	event.global_position = at
	event.button_index = MOUSE_BUTTON_LEFT
	event.pressed = true
	Input.parse_input_event(event)
	await frames(1)
	event = InputEventMouseButton.new()
	event.position = at
	event.global_position = at
	event.button_index = MOUSE_BUTTON_LEFT
	event.pressed = false
	Input.parse_input_event(event)
	await frames()


func popup_item(menu: PopupMenu, index: int) -> void:
	var font := menu.get_theme_font("font")
	var height := maxf(font.get_height(menu.get_theme_font_size("font_size")), menu.get_theme_icon("radio_checked").get_height()) + menu.get_theme_constant("v_separation")
	var top := menu.get_theme_stylebox("panel").get_margin(SIDE_TOP)
	await click(Vector2(menu.position) + Vector2(menu.size.x / 2.0, top + height * (index + 0.5)))


func capture(name: String) -> void:
	await RenderingServer.frame_post_draw
	var result := host.get_viewport().get_texture().get_image().save_png(directory.path_join(name + ".png"))
	check(result == OK, "capture " + name)


func finish_seek() -> void:
	var deadline := Time.get_ticks_msec() + 300000
	while host._caster_seeking and Time.get_ticks_msec() < deadline:
		await frames(1)
	check(not host._caster_seeking, "seek completes before timeout")
	check(host._caster.transport_error.is_empty(), "seek reports no error")
	await frames()


func run(game: Node, output: String) -> int:
	host = game
	directory = output
	check(DirAccess.make_dir_recursive_absolute(output) == OK, "capture directory")
	await frames(6)
	await key(KEY_SPACE)
	check(host._world.is_paused(), "Space pauses")
	var before: int = host._world.replay_status().frame
	await key(KEY_PERIOD)
	check(host._world.replay_status().frame == before + 1, "period steps exactly one frame")
	await click(host._caster.speed_button.get_global_rect().get_center())
	await popup_item(host._caster.speed_button.get_popup(), 5)
	check(host._world.get_time_scale() == 8.0, "speed menu selects 8x")
	check(host._caster.transport.get_global_rect().end.y <= host.get_viewport().get_visible_rect().size.y - 8, "paused transport inside vertical safe area")
	await capture("transport-paused")
	var track: Rect2 = host._caster.timeline.get_global_rect()
	await click(Vector2(track.end.x - 2, track.get_center().y))
	check(host._caster_seeking, "timeline starts forward seek")
	await capture("seek-progress")
	await finish_seek()
	var end: Dictionary = host._world.replay_status()
	check(end.frame == end.final_frame, "timeline reaches final frame")
	check(end.mismatches == 0, "all visited frames hash-check")
	check(host._caster.history.size() > 0, "actual game events indexed")
	var viewport: Vector2 = host.get_viewport().get_visible_rect().size
	check(host._caster.transport.get_global_rect().end.x <= viewport.x, "transport inside horizontal safe area")
	check(host._caster.transport.get_global_rect().end.y <= viewport.y, "transport inside vertical safe area")
	await capture("timeline-ended")
	await key(KEY_2)
	await key(KEY_T)
	var saved_view: Dictionary = host._world.caster_status()
	var saved_camera: Dictionary = host._hud.get_camera()
	var markers: int = host._caster.history.size()
	check(host._caster.marker_button.item_count > 1, "marker jump list populated")
	if host._caster.marker_button.item_count > 1:
		var target: int = host._caster.marker_button.get_item_metadata(1)
		await click(host._caster.marker_button.get_global_rect().get_center())
		await popup_item(host._caster.marker_button.get_popup(), 1)
		await finish_seek()
		var result: Dictionary = host._world.replay_status()
		check(result.frame == target, "marker jumps to exact earlier frame")
		check(result.hashes_compared == target - 1 and result.mismatches == 0, "backward seek validates every intervening hash")
		check(host._world.caster_status().observed.index == saved_view.observed.index, "rewind restores player POV")
		check(host._world.caster_status().team, "rewind restores team vision")
		check(host._world.is_paused() and host._world.get_time_scale() == 8.0, "rewind restores paused speed")
		check(host._caster.history.size() == markers and host._caster.marker_button.item_count == markers + 1, "rewind preserves indexed marker UI")
		check(host._hud.get_camera().position.distance_to(saved_camera.position) < 0.01, "rewind restores camera position")
		await capture("marker-rewind")
	print("CASTER UI RESULT failures=", failures)
	return failures

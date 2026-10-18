## Rendered PiP proof against the real command-recorded encounter.
extends "res://tests/caster_overlay_walk.gd"


func pip_bounds() -> void:
	bounds()
	var pip: Control = host._caster.pip
	check(host.get_viewport().get_visible_rect().encloses(pip.get_global_rect()), "PiP inside safe viewport")
	for panel in host._caster.overlays.panels.values():
		if panel.visible:
			check(not pip.get_global_rect().intersects(panel.get_global_rect()), "PiP does not cover an enabled overlay")


func run(game: Node, output: String) -> int:
	host = game
	directory = output
	check(DirAccess.make_dir_recursive_absolute(output) == OK, "PiP capture directory")
	await frames(6)
	await key(KEY_SPACE)
	check(host._world.is_paused(), "pause before PiP checks")
	if not host._world.is_paused():
		return failures
	await host._caster_seek(305)
	await finish_seek()
	var bases: Array[Dictionary] = []
	var fighters: Array[Dictionary] = []
	for id in host._world.get_object_ids():
		var object: Dictionary = host._world.get_object(id)
		if object.get("template", "") == "MenFortressCitadel":
			bases.append(object)
		elif object.get("template", "") == "RohanEomer":
			fighters.append(object)
	check(bases.size() == 2, "both retail bases located for the two views")
	check(fighters.size() == 2, "both fighting heroes located")
	if bases.size() != 2 or fighters.size() != 2:
		return failures
	host._hud.camera_look_at(Vector2(bases[0].x, bases[0].y))
	await frames(20)
	await key(KEY_F7)
	var pip: Control = host._caster.pip
	check(pip.enabled and pip.visible, "F7 opens PiP")
	var pinned: Vector3 = pip.target
	host._hud.camera_look_at(Vector2(bases[1].x, bases[1].y))
	await frames(20)
	check(pip.target == pinned, "main camera movement does not move the pinned target")
	check(pip.camera.get_world_3d() == host._camera.get_world_3d(), "both cameras share one presented world")
	check(pip.camera.global_basis.is_equal_approx(host._camera.global_basis), "parallel view angles preserve batch depth order")
	check(pip.camera.global_position.distance_to(host._camera.global_position) > 400, "inset really views another location")
	var before: int = host._world.get_state_hash()
	for code in [KEY_F2, KEY_F3, KEY_F4, KEY_F5]:
		await key(code)
	await frames(12)
	pip_bounds()
	await capture("pip-all")
	await key(KEY_1)
	if host._world.net_observed().name == bases[0].owner:
		await key(KEY_2)
	await frames(12)
	check(host._caster.observed >= 0 and pip.label.text.contains("POV"), "PiP follows player vision")
	var hidden := 0
	var exposed := 0
	for id in host._world.get_object_ids():
		var pose: Dictionary = host._world.get_render_pose(id)
		if pose.get("view_valid", false) and pose.presented_shrouded:
			hidden += 1
			if not pose.view_hidden:
				exposed += 1
	check(hidden > 0 and exposed == 0, "paused player view hides %d shrouded objects in the shared world (%d exposed)" % [hidden, exposed])
	await capture("pip-player")
	await key(KEY_T)
	await frames(12)
	check(pip.label.text.contains("allies"), "PiP follows team vision")
	await capture("pip-team")
	await key(KEY_0)
	var main_target: Vector3 = host._hud.get_camera().target
	await click(pip.controls.get_child(1).get_global_rect().get_center())
	await frames(12)
	check(pip.target.is_equal_approx(main_target), "Swap pins the old main target")
	check(host._hud.get_camera().position.distance_to(Vector2(pinned.x, -pinned.z)) < 1, "Swap moves the main view to the pinned target")
	await capture("pip-swapped")
	var camera_before: Dictionary = host._hud.get_camera()
	var distance_before: float = pip.distance
	var wheel := InputEventMouseButton.new()
	wheel.position = pip.picture.get_global_rect().get_center()
	wheel.global_position = wheel.position
	wheel.button_index = MOUSE_BUTTON_WHEEL_UP
	wheel.pressed = true
	Input.parse_input_event(wheel)
	await frames(8)
	check(pip.distance < distance_before, "wheel zoom reaches PiP")
	for field in ["position", "angle", "pitch", "zoom"]:
		check(host._hud.get_camera()[field] == camera_before[field], "PiP wheel leaves main camera " + field + " unchanged")
	var repin := InputEventKey.new()
	repin.keycode = KEY_F7
	repin.shift_pressed = true
	repin.pressed = true
	Input.parse_input_event(repin)
	await frames(4)
	repin.pressed = false
	Input.parse_input_event(repin)
	check(pip.target == host._hud.get_camera().target, "Shift+F7 repins the current location")
	host._hud.camera_look_at(Vector2((fighters[0].x + fighters[1].x) * 0.5, (fighters[0].y + fighters[1].y) * 0.5))
	await frames(12)
	await key(KEY_F9)
	await frames(12)
	check(pip.visible and not pip.controls.visible and not host._hud.visible, "clean mode keeps the chosen view but hides controls")
	pip_bounds()
	await capture("pip-clean")
	for code in [KEY_F2, KEY_F3, KEY_F4, KEY_F5]:
		await key(code)
	await frames(12)
	await capture("pip-broadcast")
	check(host._world.get_state_hash() == before, "PiP interactions never mutate the paused simulation")
	var saved: Dictionary = pip.preferences()
	await host._caster_seek(300)
	await finish_seek()
	pip = host._caster.pip
	await frames(12)
	check(pip.preferences() == saved and pip.visible, "rewind restores pinned target, zoom and enabled state")
	check(pip.viewport.world_3d == host._camera.get_world_3d(), "rewind attaches to the rebuilt world")
	await key(KEY_ESCAPE)
	await frames(12)
	check(host._quit_open and not pip.visible and pip.viewport.render_target_update_mode == SubViewport.UPDATE_DISABLED, "quit suspends the second renderer")
	await key(KEY_ESCAPE)
	await frames(8)
	check(pip.visible and host._world.is_paused(), "return restores PiP without unpausing")
	await key(KEY_F7)
	check(not pip.visible and pip.viewport.render_target_update_mode == SubViewport.UPDATE_DISABLED, "F7 stops hidden viewport rendering")
	saved = pip.preferences()
	await host._caster_seek(299)
	await finish_seek()
	pip = host._caster.pip
	check(pip.preferences() == saved and not pip.visible, "rewind preserves a disabled inset and its pin")
	await key(KEY_F7)
	await host._caster_seek(host._world.replay_status().final_frame)
	await finish_seek()
	check(host._world.replay_status().mismatches == 0, "all recorded CRCs match with PiP")
	await capture("pip-final")
	print("CASTER PIP UI RESULT failures=", failures)
	return failures

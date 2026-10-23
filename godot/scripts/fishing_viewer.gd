## Playable fishing demo on BFME2 Evendim. Requires --mod=<generated fishing mod>.
extends "res://scripts/hud_viewer.gd"

func _ready() -> void:
	_map_name = "map mp evendim"
	_scenario = "fishing"
	_alternate = true
	_logic_thread = false
	super._ready()

func _mount_game_files() -> Dictionary:
	var install := OS.get_environment("BFME2_INSTALL")
	if install.is_empty():
		return {"ok": false, "errors": ["Set BFME2_INSTALL to your BFME2 installation"]}
	return _fs.mount_bfme2_path(install)

func _run_scenario() -> void:
	var spots: Array[int] = []
	var point := 0
	var nearest := INF
	for id in _world.get_object_ids():
		var candidate: Dictionary = _world.get_object(id)
		if candidate.get("template", "") != "OpenBFMEFishingSpot":
			continue
		spots.append(int(id))
		var cell: Dictionary = _world.debug_pathfind_cell(candidate.x, candidate.y)
		if not cell.get("ok", false) or not cell.has("water_height") or float(cell.water_height) - float(cell.ground) < 20.0:
			_fail("Automatically placed fishing spot is outside deep water")
			return
		var distance := Vector2(candidate.x - 2250.0, candidate.y - 1800.0).length_squared()
		if distance < nearest:
			nearest = distance
			point = int(id)
	if point == 0:
		_fail("No automatic deep-water fishing spots were generated")
		return
	var target: Dictionary = _world.get_object(point)
	var boat: int = _world.create_object("OpenBFMEFishingBoat", _local_index, target.x - 100.0, target.y, 0.0)
	if boat <= 0:
		_fail("Fishing mod missing: launch with --mod=<generated fishing mod>")
		return
	print("FISHING automatic deep-water spots: ", spots.size())
	_cam_target = Vector2(target.x - 50.0, target.y - 150.0)
	_place_camera()
	_hud.set_view_options({"zoom_out_factor": 3.0})
	_hud.camera_set_height(650.0)
	_world.set_shroud_drawn(false)
	var instructions := Label.new()
	instructions.position = Vector2(10, 30)
	instructions.text = "Select the fishing ship, then right-click the fish spot. S stops fishing."
	instructions.add_theme_color_override("font_outline_color", Color.BLACK)
	instructions.add_theme_constant_override("outline_size", 4)
	_layer.add_child(instructions)
	print("FISHING demo: ship %d, point %d. Right-click the point to fish." % [boat, point])

	if not _screenshot_path.is_empty() or "--verify-fishing" in OS.get_cmdline_user_args():
		await _frames(30)
		var boat_pixel := _pick_pixel(boat)
		if boat_pixel.x < 0:
			_fail("Fishing ship is not pickable in the demo")
			return
		_hud.inject_mouse_move(boat_pixel)
		_hud.inject_mouse_button(1, true, boat_pixel, false)
		await _frames(2)
		_hud.inject_mouse_button(1, false, boat_pixel, false)
		await _frames(5)
		var point_pixel := _pick_pixel(point)
		if point_pixel.x < 0:
			print("FISHING failed pick selection: ", _hud.get_selection())
			print("FISHING boat: ", JSON.stringify(_world.get_object(boat)))
			print("FISHING point: ", JSON.stringify(_world.get_object(point)))
			var projected := _camera.unproject_position(Vector3(target.x, float(_world.get_object(point).get("z", 0.0)) + 8.0, -target.y))
			print("FISHING pick probe: ", projected, " ", JSON.stringify(_hud.pick_probe(projected, point)))
			if not _screenshot_path.is_empty():
				_save(_screenshot_path)
			_fail("Fishing spot is not pickable in the demo")
			return
		_hud.inject_mouse_move(point_pixel)
		await _frames(5)
		var cursor: String = _hud.get_state().get("cursor", "")
		if cursor != "EnterFriendly":
			_fail("Fishing hover cursor was %s, expected EnterFriendly" % cursor)
			return
		print("FISHING UI verified: selected ship and fishing cursor over the spot")
		_hud.inject_mouse_button(2, true, point_pixel, false)
		await _frames(2)
		_hud.inject_mouse_button(2, false, point_pixel, false)
		var start_frame: int = _world.get_frame()
		var deadline := Time.get_ticks_msec() + 90000
		var paid := false
		while _world.get_frame() < start_frame + 300 and Time.get_ticks_msec() < deadline:
			await _frames(1)
			paid = int(_world.get_object(boat).get("fishing_income", 0)) > 0
			if paid:
				break
		print("FISHING ship state: ", JSON.stringify(_world.get_object(boat)))
		if not paid:
			_fail("Fishing ship did not earn resources after the right-click order")
			return
		if not _screenshot_path.is_empty():
			_save(_screenshot_path)
		_finish()

func _pick_pixel(id: int) -> Vector2:
	var object: Dictionary = _world.get_object(id)
	for height in [8.0, 20.0, 40.0, 70.0]:
		var center := _camera.unproject_position(Vector3(object.x, object.z + height, -object.y))
		for offset in [Vector2.ZERO, Vector2(-12, 0), Vector2(12, 0), Vector2(0, -12), Vector2(0, 12)]:
			var pixel: Vector2 = center + offset
			if int(_hud.pick_probe(pixel, id).get("picked", 0)) == id:
				return pixel
	return Vector2(-1, -1)

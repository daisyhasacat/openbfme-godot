## Evendim capture and production regression. Requires BFME2_INSTALL and an enabled fishing mod.
extends "res://scripts/fishing_viewer.gd"
func _run_scenario() -> void:
	_world.set_shroud_drawn(false)
	_world.set_time_scale(10.0)
	var ports: Array = []
	var flags: Array = []
	var spots := 0
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		if str(o.template).begins_with("ShipWright"): ports.append(o)
		if str(o.template).begins_with("CaptureFlag"): flags.append(o)
		if o.template == "OpenBFMEFishingSpot": spots += 1
		if "creep" in str(o.get("owner", "")).to_lower(): _world.destroy_object(id)
	if spots != 8: _fail("Expected 8 automatic spots without --mod, got %d" % spots); return
	for port in ports:
		var flag: Dictionary = {}
		var distance := INF
		for f in flags:
			var d := Vector3(port.x,port.y,port.z).distance_to(Vector3(f.x,f.y,f.z))
			if d < distance: flag = f; distance = d
		var at := _free_spot(Vector2(flag.x, flag.y - 180.0))
		var party: int = _world.create_object("GondorFighterHorde", _local_index, at.x, at.y, 0.0)
		_cam_target = Vector2(flag.x,flag.y)
		_place_camera()
		_hud.set_view_options({"zoom_out_factor":3.0})
		_hud.camera_set_height(650.0)
		await _frames(12)
		await _box_select(at)
		if _hud.get_selection().is_empty(): _fail("Party not selected"); return
		await _click_world(Vector2(flag.x,flag.y),2)
		var deadline := Time.get_ticks_msec() + 20000
		while int(_world.get_object(port.id).get("player_id",-1)) != _local_index and Time.get_ticks_msec() < deadline:
			await _frames(1)
		if int(_world.get_object(port.id).get("player_id",-1)) != _local_index:
			print("CAPTURE state ",JSON.stringify(_world.get_object(flag.id))," selection ",_hud.get_selection()," HUD ",JSON.stringify(_hud.get_state()))
			_fail("Dock %d did not capture (distance %.1f)" % [port.id,distance]); return
		print("FISHING CAPTURE PASS dock %d distance %.1f" % [port.id,distance])
		_world.destroy_object(party)
	# Production through the captured dock's actual HUD button.
	var port: Dictionary = ports[0]
	_cam_target = Vector2(port.x,port.y)
	_place_camera()
	_hud.camera_set_height(650.0)
	await _frames(10)
	var pixel := _pick_pixel(port.id)
	if pixel.x < 0: _fail("Captured dock not pickable"); return
	_hud.inject_mouse_move(pixel)
	_hud.inject_mouse_button(1,true,pixel,false)
	await _frames(2)
	_hud.inject_mouse_button(1,false,pixel,false)
	await _frames(10)
	if not _hud.press_command_button("OpenBFMEFishingBoat"):
		print("PRODUCTION HUD ",JSON.stringify(_hud.get_state()))
		_fail("Fishing build button missing or disabled"); return
	var deadline := Time.get_ticks_msec() + 30000
	var boat := 0
	while boat == 0 and Time.get_ticks_msec() < deadline:
		await _frames(1)
		for id in _world.get_object_ids():
			if _world.get_object(id).get("template","") == "OpenBFMEFishingBoat": boat = id; break
	if boat == 0: _fail("Fishing boat did not finish production"); return
	print("FISHING PRODUCTION PASS boat ", boat)
	print("FISHING REGULAR GAME SMOKE PASS: 8 spots, 8 captures, fishing ship built without CLI mod")
	_finish()

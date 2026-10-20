extends SceneTree
# Build grid (snap) mode: the square under the placement ghost is drawn under the built structures near the cursor too.
# Needs ROTWK_INSTALL / BFME2_INSTALL. Optional: -- --shot=<png>

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var loaded = world.load_map("map mp fall back 4p", {"slots": [
		{"player":"Player_1", "faction":"FactionMen", "human":true},
		{"player":"Player_2", "faction":"FactionMordor", "human":false}]})
	assert(loaded.ok, str(loaded))
	var player = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then return id end end")
	assert(player.ok and not player.output.is_empty(), str(player))
	var position := Vector2(1800, 1800)
	var porter = world.create_object("MenPorter", int(player.output), position.x, position.y, 0)
	# tolerance: Object IDs are allocated dynamically; zero is the invalid ID.
	assert(porter > 0)
	var farm = world.create_object("GondorFarm", int(player.output), position.x + 250, position.y, 0)
	# tolerance: Object IDs are allocated dynamically; zero is the invalid ID.
	assert(farm > 0)
	world.advance(0.2)
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	var setup = hud.setup(fs, world, camera, "Player_1", {"camera_start":Vector3(position.x,0,-position.y), "alternate_mouse":true})
	assert(setup.ok, str(setup))
	world.set_shroud_drawn(false)
	hud.set_controller_layout(true)
	hud.select_controller_group(PackedInt64Array([porter]))
	await process_frame
	world.advance(0.2)
	await process_frame
	await process_frame
	var construction := {}
	for command in hud.get_command_buttons():
		if command.get("template", "") == "GondorFarm":
			construction = command
	assert(not construction.is_empty(), str(hud.get_command_buttons()))
	assert(hud.activate_command(construction.slot, construction.in_palantir, false))
	hud.set_placement_snap(true, 50)
	hud.inject_mouse_move(hud.world_to_pixel(position))
	for i in 8: await process_frame
	var state: Dictionary = hud.get_placement_ghost_state()
	assert(state.footprint_visible, str(state))
	assert(state.footprint_count == 1, str(state))
	assert(state.building_alignment_lines == 1, str(state))
	# tolerance: The placed farm must contribute a footprint; other registered structures may also contribute.
	assert(state.structure_footprint_count >= 1, str(state))
	assert(is_zero_approx(state.grid_angle), str(state))
	# Moving off the existing farm's row and column removes its guide.
	hud.inject_mouse_move(hud.world_to_pixel(position + Vector2(50,100)))
	for i in 8: await process_frame
	assert(hud.get_placement_ghost_state().building_alignment_lines == 0)
	hud.inject_mouse_move(hud.world_to_pixel(position))
	for i in 8: await process_frame
	# G while placing turns the grid 45 degrees; the snapped site lies on the turned grid
	var g := InputEventKey.new()
	g.keycode = KEY_G
	g.pressed = true
	Input.parse_input_event(g)
	var up := g.duplicate()
	up.pressed = false
	Input.parse_input_event(up)
	hud.inject_mouse_move(hud.world_to_pixel(position + Vector2(37, 11)))
	for i in 8: await process_frame
	state = hud.get_placement_ghost_state()
	assert(is_equal_approx(state.grid_angle, PI / 4), str(state))
	var at: Dictionary = hud.get_placement()
	var u: float = (at.x + at.y) / sqrt(2.0) / 50.0
	var v: float = (at.y - at.x) / sqrt(2.0) / 50.0
	# tolerance: World-to-grid float conversion permits one hundredth of a cell of rounding error.
	assert(absf(u - roundf(u)) < 0.01 and absf(v - roundf(v)) < 0.01, str(at))
	# The same guide follows the rotated grid, rather than world X/Y only.
	var diagonal = world.create_object("GondorFarm", int(player.output), at.x + sqrt(2.0)*100, at.y + sqrt(2.0)*100, 0)
	# tolerance: zero is create_object's failure sentinel.
	assert(diagonal > 0)
	world.advance(.2)
	for i in 8: await process_frame
	assert(hud.get_placement_ghost_state().building_alignment_lines == 1)
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shot="):
			root.get_viewport().get_texture().get_image().save_png(arg.trim_prefix("--shot="))
	Input.parse_input_event(g)
	Input.parse_input_event(up)
	for i in 4: await process_frame
	assert(is_zero_approx(hud.get_placement_ghost_state().grid_angle))
	hud.set_placement_snap(false, 50)
	for i in 4: await process_frame
	state = hud.get_placement_ghost_state()
	assert(not state.footprint_visible and state.structure_footprint_count == 0, str(state))
	assert(state.building_alignment_lines == 0, str(state))
	print("SNAP STRUCTURE FOOTPRINTS PASS: ", state)
	quit()

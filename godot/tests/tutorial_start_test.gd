## Installed-data integration: TUTORIAL_TEST_NAME=Basic|Advanced, through the actual menu callback.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	create_timer(180).timeout.connect(func(): printerr("TUTORIAL TIMEOUT"); quit(1))
	var lesson := OS.get_environment("TUTORIAL_TEST_NAME")
	assert(lesson in ["Basic", "Advanced"])
	var folder := OS.get_temp_dir().path_join("openbfme-tutorial-" + lesson)
	DirAccess.make_dir_recursive_absolute(folder)
	var options := folder.path_join("Options.ini")
	var file := FileAccess.open(options, FileAccess.WRITE)
	file.store_string("OpenBFMEMainMenuStyle = bfme2\n"); file.close()
	var game = load("res://scripts/game.gd").new()
	game._intro = "off"
	game._options_file = options
	game._save_dir_arg = folder.path_join("Save")
	root.add_child(game)
	while game._campaign_flow == null or game._state not in [game.State.MENU, game.State.FAILED]: await process_frame
	assert(game._state == game.State.MENU, game._failed)
	await game._wait_main_ready()
	game._world.set_logic_thread(false)
	game._campaign_flow.hooks = true
	var def: Dictionary = game._world.get_tutorial(lesson)
	assert(def.ok, str(def))
	assert(not String(def.map).is_empty())
	var refused: Dictionary = game._world.start_campaign_mission({"tutorial": lesson, "record_memory": true})
	assert(refused.errors == ["Tutorial save recording is not implemented (S-175)"])
	assert(game._world.get_tutorial("Strategic").error == "[S-175] Strategic Tutorial requires the unported War of the Ring tutorial flow")
	assert(game._shell.shell_fscommand("AptMainMenu::OnTutorial", "Strategic"))
	await process_frame
	await process_frame
	assert(game._state == game.State.MENU)
	assert(game._campaign_flow.tutorial_error == "[S-175] Strategic Tutorial requires the unported War of the Ring tutorial flow")
	assert(game._shell.shell_fscommand("AptMainMenu::OnTutorial", lesson))
	while game._state not in [game.State.PLAYING, game.State.FAILED]: await process_frame
	assert(game._state == game.State.PLAYING, game._failed)
	assert(game._campaign_flow.tutorial == lesson)
	assert(game._campaign_flow.campaign.is_empty())
	assert(game._hud_installed)
	_assert_unexplored_shroud(game._world)
	if lesson == "Basic": _assert_opening_reveal(game._world)
	assert(game._report.stops.has("[S-175] tutorial entry: Objective and loading voice presentation are not ported; tutorial saves are not recorded"))
	assert(game._report.map.to_lower() == String(def.map).to_lower())
	_assert_camera_start(game)
	if lesson == "Basic":
		_assert_scripted_camera_move(game)
		await _assert_camera_controls(game)
		await _assert_spotlight(game)
	await _assert_native_objectives(game)
	# Restart uses the same standalone tutorial, without advancing campaign progress.
	await game._campaign_flow.restart_mission()
	assert(game._state == game.State.PLAYING)
	assert(game._campaign_flow.tutorial == lesson)
	assert(not game._objectives_open)
	assert(not game._shell.shell_stack().has("Objectives.apt"))
	assert(game._hud_installed)
	_assert_unexplored_shroud(game._world)
	if lesson == "Basic": _assert_opening_reveal(game._world)
	var loaded_map := String(game._report.map)
	# Leaving and Continue return to the main menu without campaign autosaves or unlocks.
	game._campaign_flow.leave_to_score_screen()
	assert(game._campaign_flow.score_pending)
	await game._campaign_flow.after_score()
	assert(game._state == game.State.MENU)
	assert(game._campaign_flow.tutorial.is_empty())
	for i in 60: await process_frame
	while game._shell_pictures_pending != 0: await process_frame
	print("TUTORIAL START PASS: ", lesson, " ", loaded_map)
	game.free()
	quit()

func _assert_unexplored_shroud(world: Node) -> void:
	var report: Dictionary = world.get_shroud_report()
	assert(report.displayed)
	var cells: PackedByteArray = world.get_shroud_cells()
	assert(cells.has(2)) # never explored cells remain shrouded
	var hidden := cells.find(2)
	var x := hidden % int(report.cells_x)
	var y := hidden / int(report.cells_x)
	var checked := false
	for mesh in world.get_node("Terrain").find_children("*", "MeshInstance3D", true, false):
		for surface in mesh.mesh.get_surface_count():
			var material = mesh.get_active_material(surface)
			if material is ShaderMaterial and material.shader.code.contains("terrain_common"):
				assert(material.get_shader_parameter("shroud_enabled") == true)
				var texture: Texture2D = material.get_shader_parameter("shroud_tex")
				assert(texture != null)
				# The texture has a one-texel border. Unexplored cells multiply terrain RGB by zero.
				assert(texture.get_image().get_pixel(x + 1, y + 1).r == 0.0)
				checked = true
				break
		if checked: break
	assert(checked, "A terrain material must bind the black unexplored shroud texture")

func _assert_opening_reveal(world: Node) -> void:
	# Retail Basic tutorial Map Setup: permanent radius 550 at this waypoint, plus its start-area trigger.
	var waypoint: Dictionary = world.get_waypoint("WP - Move Fighters Here")
	assert(waypoint.ok)
	assert(world.get_shroud_status_at(waypoint.x, waypoint.y) == 0)
	var report: Dictionary = world.get_shroud_report()
	var x := floori(float(waypoint.x) / float(report.cell_size))
	var y := floori(float(waypoint.y) / float(report.cell_size))
	var checked := false
	for mesh in world.get_node("Terrain").find_children("*", "MeshInstance3D", true, false):
		for surface in mesh.mesh.get_surface_count():
			var material = mesh.get_active_material(surface)
			if material is ShaderMaterial and material.shader.code.contains("terrain_common"):
				var texture: Texture2D = material.get_shader_parameter("shroud_tex")
				assert(texture.get_image().get_pixel(x + 1, y + 1).r == 1.0)
				checked = true
				break
		if checked: break
	assert(checked, "The scripted opening reveal must reach the terrain's shroud texture")

func _assert_camera_start(game: Node) -> void:
	var cine: Node = game._campaign_flow._cine
	var start: Vector3 = game._report.start.camera_start
	assert(Vector2(cine.target.x, cine.target.y) == Vector2(start.x, -start.z))
	var state: Dictionary = game._hud.get_camera()
	assert(game._camera.position == state.eye)
	assert(Vector2(cine.target.x, cine.target.y) == state.position)
	assert(cine._base_height == state.height_above_ground)

func _assert_scripted_camera_move(game: Node) -> void:
	var cine: Node = game._campaign_flow._cine
	game._world.set_auto_advance(false)
	cine.set_process(false)
	cine._process(0.0) # consume the opening input lock and letterbox at the initial position
	var start: Vector3 = cine.target
	var waypoint: Dictionary = game._world.get_waypoint("WP - Spotlight Fighters")
	assert(waypoint.ok)
	assert(game._world.debug_script_action("MOVE_CAMERA_TO", [
		{"type":51, "string":"WP - Spotlight Fighters"}, {"type":1, "real":8.0},
		{"type":1, "real":0.0}, {"type":1, "real":0.0}, {"type":1, "real":1.0}]).ok)
	game._world.advance(0.2) # publish the injected script request
	cine._process(0.0)
	assert(Vector2(cine.target.x, cine.target.y) == Vector2(start.x, start.y))
	cine._process(8.0)
	assert(Vector2(cine.target.x, cine.target.y) == Vector2(waypoint.x, waypoint.y))
	var state: Dictionary = game._hud.get_camera()
	assert(state.position == Vector2(waypoint.x, waypoint.y))
	assert(game._camera.position == state.eye)
	# The same tactical state remains when the script returns input to the player.
	assert(game._world.debug_script_action("CAMERA_LETTERBOX_END", []).ok)
	assert(game._world.debug_script_action("ENABLE_INPUT", []).ok)
	game._world.advance(0.2)
	cine._process(0.0)
	assert(game._hud.get_camera().position == state.position)

func _assert_camera_controls(game: Node) -> void:
	var hud: Node = game._hud
	assert(game._world.get_script_report().camera_scroll_distance == 0.0, "Scripted opening pan must not count as player scrolling")
	var centre := root.get_visible_rect().size * 0.5
	hud.inject_mouse_button(MOUSE_BUTTON_RIGHT, true, centre, false)
	hud.inject_mouse_move(centre + Vector2(180, 0))
	await create_timer(1.2).timeout
	hud.inject_mouse_button(MOUSE_BUTTON_RIGHT, false, centre + Vector2(180, 0), false)
	var travelled: float = hud.get_camera().user_scroll_distance
	# tolerance: BFME2's tutorial explicitly requires accumulated scroll travel strictly beyond 250 world units.
	assert(travelled > 250.0, "Right mouse drag must accumulate the tutorial's required camera travel")
	game._world.advance(0.2)
	assert(game._world.get_script_report().camera_scroll_distance == travelled)

func _assert_native_objectives(game: Node) -> void:
	var world: Node = game._world
	var cine: Node = game._campaign_flow._cine
	world.set_auto_advance(false)
	cine.set_process(false)
	for action in ["ENABLE_INPUT", "CAMERA_LETTERBOX_END", "ENABLE_OBJECTIVES_SCREEN"]:
		assert(world.debug_script_action(action, []).ok)
	world.advance(0.2)
	cine._process(0.0)
	game._palantir_objectives()
	assert(game._objectives_open)
	assert(game._shell.shell_stack()[-1] == "Objectives.apt")
	assert(game._shell.get_backdrop_state().background_mode == 2)
	assert(not world.is_paused(), "BFME2 tutorial mode keeps simulation running for the objectives-open check")
	assert(not cine._objectives_panel.visible, "The native movie replaces the Godot objectives panel")
	world.advance(0.2)
	assert(world.get_script_report().objectives_open)
	await create_timer(1.0).timeout
	var buttons: Array = []
	for button in game._shell.list_buttons(game._shell.shell_top_level()):
		if button.hittable and String(button.path).contains(".Resume."): buttons.append(button)
	assert(buttons.size() == 1, str(buttons))
	var shot := OS.get_environment("OBJECTIVES_TEST_SHOT")
	if not shot.is_empty():
		await RenderingServer.frame_post_draw
		assert(root.get_texture().get_image().save_png(shot) == OK)
	var centre := Vector2(buttons[0].x, buttons[0].y)
	game._mouse_event(centre, false, false)
	await game._step(2)
	game._mouse_event(centre, true, true)
	await game._step(2)
	game._mouse_event(centre, false, true)
	# The retail movie sends ReturnToGame after its button/out animation.
	for attempt in 120:
		if not game._objectives_open: break
		await create_timer(0.02).timeout
	assert(not game._objectives_open)
	assert(not game._shell.shell_stack().has("Objectives.apt"))
	world.advance(0.2)
	assert(not world.get_script_report().objectives_open)
	# The script gate blocks opening; enabling it allows another native instance.
	assert(world.debug_script_action("DISABLE_OBJECTIVES_SCREEN", []).ok)
	world.advance(0.2)
	cine._process(0.0)
	game._palantir_objectives()
	assert(not game._objectives_open)
	assert(world.debug_script_action("ENABLE_OBJECTIVES_SCREEN", []).ok)
	world.advance(0.2)
	cine._process(0.0)
	game._palantir_objectives()
	assert(game._objectives_open)
	assert(world.debug_script_action("DISABLE_INPUT", []).ok)
	world.advance(0.2)
	cine._process(0.0)
	assert(game._shell.shell_fscommand("AptObjectivesMenu::ReturnToGame", ""))
	for i in 2: await process_frame
	assert(game._objectives_open, "Tutorial input lock keeps its objectives lesson on screen")
	await game._press_escape()
	assert(game._objectives_open)
	assert(world.debug_script_action("ENABLE_INPUT", []).ok)
	world.advance(0.2)
	cine._process(0.0)
	assert(world.debug_script_action("CLOSE_OBJECTIVES_SCREEN", []).ok)
	world.advance(0.2)
	cine._process(0.0)
	assert(not game._objectives_open)
	world.advance(0.2)
	assert(not world.get_script_report().objectives_open)
	game._palantir_objectives()
	assert(game._objectives_open)
	await game._press_escape()
	assert(not game._objectives_open)
	world.advance(0.2)
	assert(not world.get_script_report().objectives_open)

	# The same single-player economy mode pauses for a campaign rather than a tutorial.
	var lesson: String = game._campaign_flow.tutorial
	game._campaign_flow.tutorial = ""
	game._palantir_objectives()
	assert(world.is_paused())
	game._close_objectives()
	assert(not world.is_paused())
	game._campaign_flow.tutorial = lesson
	# The caller restarts while this instance is open, exercising teardown cleanup.
	game._palantir_objectives()
	assert(game._objectives_open)

func _spotlight_node(world: Node, object_id: int) -> Decal:
	for node in world.find_children("DrawableAdditiveDecal_*", "Decal", false, false):
		if int(node.get_meta("object_id", 0)) == object_id: return node
	return null

func _assert_spotlight(game: Node) -> void:
	var world: Node = game._world
	var waypoint: Dictionary = world.get_waypoint("WP - Spotlight Fighters")
	assert(world.debug_script_action("CREATE_NAMED_ON_TEAM_AT_WAYPOINT", [
		{"type":14, "string":"Test Spotlight"}, {"type":15, "string":"InGameMarker"},
		{"type":3, "string":"PlyrCivilian/teamPlyrCivilian"}, {"type":7, "string":"WP - Spotlight Fighters"}]).ok)
	world.advance(0.2)
	for i in 3: await process_frame
	var marker: Dictionary = world.debug_script_unit("Test Spotlight")
	assert(marker.ok, str(marker))
	var decal := _spotlight_node(world, marker.id)
	assert(decal != null, "The retail marker's additive shadow must create a ground decal")
	assert(decal.visible)
	assert(decal.size == Vector3(135.0, 200.0, 135.0))
	assert(decal.position == Vector3(marker.x, marker.z, -marker.y))
	assert(decal.cull_mask == 524288) # terrain layer, bit 19
	assert(decal.get_texture(Decal.TEXTURE_EMISSION) != null)
	assert(decal.get_texture(Decal.TEXTURE_ALBEDO) == null)
	assert(decal.get_meta("shadow_texture") == "EXSpotLight.tga")
	assert(decal.get_meta("acceptance_stop") == "[S-3304] additive drawable decals: projection/blend and ShadowOpacity timing are unverified")
	var shot := OS.get_environment("SPOTLIGHT_TEST_SHOT")
	if not shot.is_empty():
		game._hud.camera_look_at(Vector2(waypoint.x, waypoint.y))
		game._hud.camera_set_height(400.0)
		game._campaign_flow._cine._process(0.0)
		await create_timer(0.5).timeout
		await RenderingServer.frame_post_draw
		assert(root.get_texture().get_image().save_png(shot) == OK)
	# Position follows the ordinary drawable transform, not a fixed tutorial coordinate.
	assert(world.debug_script_place_at("Test Spotlight", waypoint.x + 40.0, waypoint.y + 20.0).ok)
	world.advance(0.2)
	for i in 3: await process_frame
	marker = world.debug_script_unit("Test Spotlight")
	assert(decal.position == Vector3(marker.x, marker.z, -marker.y))
	assert(world.debug_script_action("NAMED_KILL", [{"type":14, "string":"Test Spotlight"}]).ok)
	for i in 10:
		world.advance(0.2)
		await process_frame
	var remaining := _spotlight_node(world, marker.id)
	assert(remaining == null or not remaining.visible, "Removing the scripted marker must remove its ground spotlight")

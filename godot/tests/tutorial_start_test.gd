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
	# Restart uses the same standalone tutorial, without advancing campaign progress.
	await game._campaign_flow.restart_mission()
	assert(game._state == game.State.PLAYING)
	assert(game._campaign_flow.tutorial == lesson)
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

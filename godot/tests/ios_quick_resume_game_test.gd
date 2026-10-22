## Full game host + actual SaveFlow/HUD. Run capture then restore in separate processes.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	create_timer(180).timeout.connect(func(): printerr("QUICK RESUME GAME TIMEOUT"); quit(1))
	var folder := OS.get_environment("RESUME_TEST_DIR")
	var phase := OS.get_environment("RESUME_TEST_PHASE")
	assert(not folder.is_empty() and phase in ["capture", "restore"])
	var game = load("res://tests/ios_quick_resume_game_host.gd").new()
	game._intro = "off"
	game._options_file = folder.path_join("Options.ini")
	game._save_dir_arg = folder.path_join("Save")
	root.add_child(game)
	while game._state not in [game.State.MENU, game.State.FAILED] or game._campaign_flow == null:
		await process_frame
	assert(game._state == game.State.MENU)
	var service = load("res://scripts/ios_quick_resume.gd").new()
	service.enabled = true; service.game = game; service.directory = folder
	game._ios_quick_resume = service; game.add_child(service); service.set_process(false)
	if phase == "capture":
		var slots := []
		for i in 8: slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
		for i in 2: slots[i] = {"state": 6, "name": "Resume host %d" % i, "accepted": true, "color": i, "start_pos": i, "player_template": 4, "team": i}
		await game._begin_game({"mode": "skirmish", "map_crc": 0, "map_size": 0, "map": "maps/map wor buckland/map wor buckland.map", "seed": 42, "starting_cash": 2000, "slots": slots})
		assert(game._state == game.State.PLAYING)
		for i in 5: game._world.advance(0.2)
		var camera: Dictionary = game._hud.get_camera()
		game._hud.camera_look_at(camera.position + Vector2(100, 150))
		game._hud.camera_set_angle_pitch(0.7, 0.8)
		var builders: Array = game._hud.get_controller_objects("builders")
		assert(not builders.is_empty())
		assert(game._hud.select_object_with_message(int(builders[0].id)))
		game._world.advance(0.2)
		await process_frame
		await process_frame
		game._toggle_quit_menu()
		service.notification(Node.NOTIFICATION_APPLICATION_PAUSED)
		assert(service.last_result.ok, str(service.last_result))
		assert(service._read("current.state").quit_open)
		assert(not service._read("current.state").selection.is_empty())
	else:
		var state: Dictionary = service._read("current.state")
		assert(await service.try_restore())
		assert(game._state == game.State.PLAYING and game._hud_installed)
		assert(game._world.get_frame() == state.frame)
		assert(game._world.get_state_hash() == state.hash)
		assert(game._world.is_paused() and game._quit_open)
		var camera: Dictionary = game._hud.get_camera()
		assert(camera.position == state.camera.position)
		assert(camera.angle == state.camera.angle and camera.pitch == state.camera.pitch)
		assert(camera.height_above_ground == state.camera.height_above_ground)
		assert(not FileAccess.file_exists(service._path("attempt.state")))
		assert(PackedInt64Array(game._hud.get_selection()) == state.selection)
		game._exit_to_score_screen()
		assert(not FileAccess.file_exists(service._path("current.state")))
	game.free()
	await process_frame
	print("IOS QUICK RESUME GAME %s PASS" % phase.to_upper())
	quit()

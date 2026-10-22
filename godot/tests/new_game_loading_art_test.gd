## The new-game player loading screen keeps the ring backdrop until gameplay.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	create_timer(180).timeout.connect(func(): printerr("LOADING ART TIMEOUT"); quit(1))
	var folder := OS.get_environment("RESUME_TEST_DIR")
	assert(not folder.is_empty())
	DirAccess.make_dir_recursive_absolute(folder)
	var game = load("res://tests/new_game_loading_art_host.gd").new()
	game._intro = "off"
	game._options_file = folder.path_join("Options.ini")
	game._save_dir_arg = folder.path_join("Save")
	root.add_child(game)
	while game._state not in [game.State.MENU, game.State.FAILED] or game._campaign_flow == null:
		await process_frame
	assert(game._state == game.State.MENU)
	var slots := []
	for i in 8: slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	for i in 2: slots[i] = {"state": 6, "name": "Loading test %d" % i, "accepted": true, "color": i, "start_pos": i, "player_template": 4, "team": i}
	await game._begin_game({"mode": "skirmish", "map_crc": 0, "map_size": 0, "map": "maps/map wor buckland/map wor buckland.map", "seed": 42, "starting_cash": 2000, "slots": slots})
	assert(game._state == game.State.PLAYING and game.checked_loading_art)
	game._shell.tick(0.033)
	game._shell.render(true)
	assert(game._shell.get_backdrop_state().backdrop_image == "")
	game.free()
	await process_frame
	print("NEW GAME LOADING ART PASS")
	quit()

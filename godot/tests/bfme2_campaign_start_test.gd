## Starts via the actual BFME2 main-menu callback, including difficulty routing.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	create_timer(180).timeout.connect(func(): printerr("BFME2 CAMPAIGN TIMEOUT"); quit(1))
	var alignment := OS.get_environment("CAMPAIGN_TEST_ALIGNMENT")
	assert(alignment in ["good", "evil"])
	var folder := "/tmp/openbfme-campaign-start-" + alignment
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
	assert(game._state == game.State.MENU)
	assert(game._menu_style == "bfme2")
	await game._wait_main_ready()
	game._campaign_flow.movies = false
	assert(game._shell.shell_fscommand("AptMainMenu::" + alignment.capitalize() + "Campaign", "H"))
	while game._state not in [game.State.PLAYING, game.State.FAILED]: await process_frame
	assert(game._state == game.State.PLAYING, game._failed)
	assert(game._campaign_flow.campaign == alignment.to_upper() + "_CAMPAIGN")
	assert(game._campaign_flow.mission == 0 and game._campaign_flow.difficulty == 2)
	assert(game._hud_installed)
	assert(game._report.map.to_lower() == game._campaign_flow.missions_of(game._campaign_flow.campaign)[0].map.to_lower())
	print("BFME2 CAMPAIGN START PASS: ", alignment, " ", game._report.get("map", ""))
	game.free()
	quit()

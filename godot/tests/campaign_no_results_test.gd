## Installed-data integration: campaign wins, defeats and completion skip results.
extends SceneTree

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	create_timer(180).timeout.connect(func(): printerr("CAMPAIGN NO RESULTS TIMEOUT"); quit(1))
	var folder := OS.get_temp_dir().path_join("openbfme-campaign-no-results")
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
	var flow: Node = game._campaign_flow
	flow.movies = false
	flow.hooks = true
	for outcome in ["win", "defeat", "complete"]:
		flow.campaign = "GOOD_CAMPAIGN"
		flow.tutorial = ""
		flow.mission = 7 if outcome == "complete" else 0
		flow.last_auto_save = ""
		flow.last_unlock = {}
		flow.menu_log.clear()
		await flow.begin_mission(false)
		assert(game._state == game.State.PLAYING, game._failed)
		game._world.set_auto_advance(false)
		if outcome == "win": assert(flow.step("won").ok)
		# Exercise the shared results entry guard and the actual quit-menu exit path.
		if outcome == "defeat": game._quit_menu_exit()
		elif outcome == "win": game._exit_to_score_screen()
		else:
			# Natural completion: the script victory and its end timer drive CampaignFlow.tick.
			assert(game._world.debug_script_victory().ok)
			for frame in 40:
				if not flow.active: break
				game._world.advance(0.2)
				flow.tick()
			assert(not flow.active)
		while game._state not in [game.State.MENU, game.State.FAILED]:
			assert(game._state != game.State.SCORE)
			assert(not game._shell.shell_stack().has("TimeLine.apt"))
			await process_frame
		assert(game._state == game.State.MENU, game._failed)
		assert(game._score_report.is_empty())
		assert(not game._shell.shell_stack().has("TimeLine.apt"))
		assert(not game._shell.shell_stack().has("ScoreScreen.apt"))
		assert(not flow.active)
		if outcome == "complete":
			assert(not flow.menu_open)
			assert(game._shell.shell_stack()[-1] == "MainMenu.apt")
			assert(flow.last_unlock.get("ok", false), str(flow.last_unlock))
			assert(game._campaign_complete)
		else:
			assert(flow.menu_open)
			assert(game._shell.shell_stack()[-1] == "CampaignMenu.apt")
			assert(flow.mission == (1 if outcome == "win" else 0))
			assert(flow.victorious == (outcome == "win"))
			if outcome == "win": assert(FileAccess.file_exists(flow.last_auto_save))
			else: assert(flow.last_auto_save.is_empty())
		print("CAMPAIGN NO RESULTS: ", outcome, " PASS")
	print("CAMPAIGN NO RESULTS PASS")
	game.free()
	quit()

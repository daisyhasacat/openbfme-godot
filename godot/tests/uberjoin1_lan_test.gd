## Lane UBERJOIN-1 (round 2): the real LAN lifecycle of a Normal player who joins an Uber game: join (the mode box's Yes) -> play -> Quit -> score ->
## Continue. The world must run uber while the game is on, and after the quit be back at the player's own mode (faithful, nothing switched, no LAN room
## held), and Continue must show the lobby again without an error (it once tried to re-open a --net-* address and failed with "bad address").
## The host is a second process (an Uber player: its Options.ini says OpenBFMEUberPerformance = yes); this process is the joiner, driven through the game
## scene as a player would (the LAN lobby's movie, the Yes of the box, the quit menu's Exit, the score screen's Continue).
##
##   godot --headless --path godot --script res://tests/uberjoin1_lan_test.gd -- --lan=join --net-name=UjJoiner --lan-players=2 --lan-targets=127.0.0.1 --lan-port-base=N --options-file=F
## and the host: godot --headless --path godot -- --lan=host --net-name=UjHost --lan-players=2 --lan-targets=127.0.0.1 --lan-port-base=N --options-file=H --map=<an 8 player map key>
## (tests/start_test.gd starts both). Needs ROTWK_INSTALL and BFME2_INSTALL (SKIP, exit 77, without). Exit codes: 0 pass, 1 fail, 2 timeout, 77 skip.
extends SceneTree

var game: Node
var _failures := 0


func _initialize() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("UBERJOIN1LAN SKIP: ROTWK_INSTALL / BFME2_INSTALL are not set")
		quit(77)
		return
	call_deferred("_run")


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _run() -> void:
	game = load("res://scenes/game.tscn").instantiate()
	root.add_child(game)
	var deadline := Time.get_ticks_msec() + 120000
	var answered := false
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if game._state == game.State.FAILED:
			print("UBERJOIN1LAN FAIL early: ", game._failed)
			quit(1)
			return
		if not answered and game._lan_phase == 2 and game._world != null:
			var answer: Dictionary = game._shell.lobby_apply({"box": "Yes"}) # the mode box: Yes, switch to Uber and join
			if answer.ok:
				answered = true
				_check(game._world.simulation_profile() == "uber" and game._world.profile_switched(), "Yes on the mode box: the world runs uber, switched")
				print("UBERJOIN1LAN identity after Yes: lobby ", game._world.lan_status().get("identity"), ", world ", game._world.net_profile_digest())
		if game._state == game.State.PLAYING:
			_check(answered, "the game started after the mode box's Yes")
			_check(game._world.simulation_profile() == "uber" and game._world.profile_switched(), "playing: uber, switched (the player's own setting is faithful)")
			_check(game._world.lan_status().get("in_game", false), "playing: the LAN room is held")
			await create_timer(2.0).timeout
			game._quit_menu_exit() # Esc, Exit: the player quits the game
			_check(game._state == game.State.SCORE, "Quit: the score screen")
			_check(game._world.simulation_profile() == "faithful" and not game._world.profile_switched(), "Quit: back at the player's own mode (faithful, nothing switched)")
			_check(not game._world.lan_status().get("in_game", true), "Quit: the LAN room is left (the game's players are gone with it)")
			await game._score_continue()
			await process_frame
			_check(game._state != game.State.FAILED and game._failed.is_empty(), "Continue works (no failure: %s)" % str(game._failed))
			_check(game._world.simulation_profile() == "faithful" and not game._world.profile_switched(), "after Continue: still the player's own mode")
			_check(game._shell.shell_stack().has("LanLobby.apt"), "after Continue: the LAN lobby is on the screen stack %s" % str(game._shell.shell_stack()))
			print("UBERJOIN1LAN ", "PASS" if _failures == 0 else "FAIL")
			quit(0 if _failures == 0 else 1)
			return
	print("UBERJOIN1LAN TIMEOUT state=", game._state, " lan_phase=", game._lan_phase)
	quit(2)

## Lane ONLINEHUB-1: game.gd --onlinehub1-check --platform-url=URL, run by tools/platform_fake/test_onlinehub1_game.py against the platform's local fake
## (tools/platform_fake/fake_server.py, its first POST /v1/results answered 503) with OPENBFME_PLATFORM_RETRY_S=3 and a user folder of its own. The
## game's own path from the score screen (game.gd _record_finished_game: the record completed by the host, AptMenuPlayer.record_game, the platform
## client, scripts/online_http.gd) is driven with five finished games:
##   g1 an online game (shared): reported; its first POST fails (503) and is sent once more after the retry time;
##   g2 a LAN game (shared): reported;
##   g3 a skirmish against the computer: recorded, never reported (single player);
##   g4 an online game with "Share my game results for statistics" off: recorded, not reported;
##   g1 again: refused (recorded already), nothing sent;
##   g5 an online game (shared) whose first POST fails (503) and whose retry is waiting when the player turns sharing off: dropped, never sent again.
## The harness then checks the fake's log: exactly the POSTs g1, g2, g1, g5. Prints "HUB1 <name>: ok / FAIL <why>" and "HUB1 RESULT: <n> ok, <m> failed".
extends RefCounted

var game: Node
var shell: Node
var ok_count := 0
var fail_count := 0


func check(name: String, cond: bool, detail := "") -> void:
	if cond:
		ok_count += 1
		print("HUB1 %s: ok" % name)
	else:
		fail_count += 1
		print("HUB1 %s: FAIL %s" % [name, detail])


func _base(winner: int, human2: bool) -> Dictionary:
	return {"v": 1, "map": "maps/map mp fords of isen/map mp fords of isen.map", "duration_s": 600, "me": 0, "winner_team": winner,
		"players": [{"slot": 0, "nick": "Checker", "faction": "FactionMen", "team": 0, "human": true},
			{"slot": 1, "nick": "Rival" if human2 else "Computer", "faction": "FactionMordor", "team": 1, "human": human2}]}


func _finish(kind: String, id: String, winner: int, human2: bool) -> void:
	game._hub_kind = kind
	game._hub_game_id = id
	game._game_start_unix = int(Time.get_unix_time_from_system()) - 600
	game._record_finished_game({"record": _base(winner, human2)})


func _wait(cond: Callable, seconds: float) -> bool:
	var until := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < until:
		if cond.call():
			return true
		await game.get_tree().process_frame
	return false


func _records_lines() -> PackedStringArray:
	var path: String = game.RECORDS_FILE
	return FileAccess.get_file_as_string(path).split("\n", false) if FileAccess.file_exists(path) else PackedStringArray()


func run(p_game: Node) -> int:
	game = p_game
	shell = game._shell
	await game._wait_main_ready()
	await game._step(30)
	check("the platform client is set up", game._platform_ready and int(shell.platform_status().get("records", -1)) == 0,
		str(shell.platform_status()))
	var install: String = FileAccess.get_file_as_string(game.INSTALL_ID_FILE).strip_edges()
	check("the install id is 32 hex digits, kept in the user folder", install.length() == 32 and install.is_valid_hex_number(), install.substr(0, 6))
	shell.set_option("OpenBFMEShareResults", "yes")
	# g1: online, shared; its first POST fails (the fake's 503) and goes once more after the retry time
	_finish("online", "hub1-g1", 0, true)
	check("g1 recorded", _records_lines().size() == 1, str(_records_lines()))
	var failed_once: bool = await _wait(func(): return int(shell.platform_status().get("pending_results", 0)) == 1, 20.0)
	check("g1's first POST failed and waits for its retry", failed_once, str(shell.platform_status()))
	# g2: a LAN game, shared
	_finish("lan", "lan-0123456789abcdef0123456789abcdef", 1, true)
	var three: bool = await _wait(func(): return int(shell.platform_status().get("results_sent", 0)) == 3 and int(shell.platform_status().get("pending_results", 1)) == 0, 30.0)
	check("g2 sent, then g1 sent again after the retry time", three, str(shell.platform_status()))
	# g3: a skirmish against the computer: recorded, never sent
	_finish("skirmish", "sk-hub1g3", 0, false)
	await game._step(60)
	check("g3 recorded, not sent (single player)", _records_lines().size() == 3 and int(shell.platform_status().get("results_sent", 0)) == 3,
		str(shell.platform_status()))
	# g4: online with the option off: recorded, not sent
	shell.set_option("OpenBFMEShareResults", "no")
	_finish("online", "hub1-g4", 1, true)
	await game._step(60)
	check("g4 recorded, not sent (the player does not share)", _records_lines().size() == 4 and int(shell.platform_status().get("results_sent", 0)) == 3,
		str(shell.platform_status()))
	shell.set_option("OpenBFMEShareResults", "yes")
	# g1 again: refused, nothing sent, nothing written
	_finish("online", "hub1-g1", 0, true)
	await game._step(60)
	check("g1 again: refused, nothing sent or written", _records_lines().size() == 4 and int(shell.platform_status().get("results_sent", 0)) == 3,
		str(shell.platform_status()))
	# g5: the first POST fails (the harness set the fake to answer the next one 503) and the retry waits; the player turns sharing off meanwhile
	_finish("online", "hub1-g5", 0, true)
	var g5_failed: bool = await _wait(func(): return int(shell.platform_status().get("pending_results", 0)) == 1 and int(shell.platform_status().get("results_sent", 0)) == 4, 20.0)
	check("g5's POST failed and its retry waits", g5_failed, str(shell.platform_status()))
	shell.set_option("OpenBFMEShareResults", "no")
	var dropped: bool = await _wait(func(): return int(shell.platform_status().get("pending_results", 1)) == 0, 5.0)
	check("turning sharing off dropped g5's waiting retry", dropped, str(shell.platform_status()))
	await game._step(300) # well past the retry time (3 s)
	check("g5's retry never went out", int(shell.platform_status().get("results_sent", 0)) == 4, str(shell.platform_status()))
	shell.set_option("OpenBFMEShareResults", "yes")
	# the file reads back: four games, none refused; Your stats count them (the Skirmish stats page's numbers)
	var rr: Dictionary = shell.set_game_records(FileAccess.get_file_as_string(game.RECORDS_FILE))
	check("the record file reads back: 5 games, none refused", int(rr.get("count", 0)) == 5 and (rr.get("rejected", []) as Array).is_empty(), str(rr))
	print("HUB1 RESULT: %d ok, %d failed" % [ok_count, fail_count])
	return fail_count

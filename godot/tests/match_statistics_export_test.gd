## OpenBFME. GPL-3.0. Real retail-data score export, independent of the presentation end-screen timing.
extends SceneTree

var failures := 0


func check(ok: bool, description: String) -> void:
	print("STATISTICS EXPORT ", "PASS " if ok else "FAIL ", description)
	if not ok:
		failures += 1


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("STATISTICS EXPORT SKIP: ROTWK_INSTALL and BFME2_INSTALL required")
		quit(77)
		return
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_retail()
	check(mounted.ok, "mount retail")
	if not mounted.ok:
		quit(1)
		return
	var world: Node3D = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	world.set_auto_advance(false)
	check(world.setup(fs).ok, "setup")
	var slots: Array = []
	for i in 8:
		slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	slots[0] = {"state": 6, "name": "Westfold", "accepted": true, "color": 0, "start_pos": 0, "player_template": 3, "team": 0}
	slots[1] = {"state": 4, "name": "Mordor", "accepted": true, "color": 1, "start_pos": 1, "player_template": 6, "team": 1}
	var prepared: Dictionary = world.prepare_new_game({"mode": "skirmish", "difficulty": 1, "map": "maps/map mp fall back 4p/map mp fall back 4p.map",
		"map_crc": 0, "map_size": 0, "map_mask": 0, "seed": 4932, "starting_cash": 1500, "slots": slots})
	check(prepared.ok, "prepare fixture")
	if not prepared.ok:
		quit(1)
		return
	check(world.start_new_game({"logic_thread": false}).ok, "start fixture")
	for i in 10:
		world.advance(0.2)
	check(world.self_destruct(false).ok, "surrender recorded")
	for i in 20:
		world.advance(0.2)
	var exported: Dictionary = world.clear_game_data(true)
	check(exported.ok, "score screen exported")
	var record: Dictionary = exported.get("record", {})
	var statistics: Dictionary = record.get("statistics", {})
	check(statistics.get("v") == 1, "statistics version")
	check(statistics.get("players", []).size() == 2, "every occupied player exported")
	var score: Array = exported.get("score_screen", {}).get("entries", [])
	var local_found := false
	for player in statistics.get("players", []):
		check(player.stats.has("structures_built") and player.stats.has("money_earned") and player.has("templates") and player.has("timeline"), "detailed counters, templates and timeline")
		check(player.stats.session_seconds == (5 if player.slot == record.me else 6), "duration uses the existing defeat frame or current frame at five Hz")
		if player.slot == record.me:
			local_found = true
			check(player.result == 1, "surrender is a loss")
			check(player.stats.units_built == score[0].units_built and player.stats.structures_lost == score[0].structures_lost, "report equals existing score screen")
	check(local_found, "local player present")
	var output := OS.get_environment("REPORT_EXPORT_FILE")
	if not output.is_empty():
		var file := FileAccess.open(output, FileAccess.WRITE)
		check(file != null, "export artifact opened")
		if file != null:
			file.store_string(JSON.stringify(statistics))
			file.close()
	var lobby := OS.get_environment("REPORT_TEST_URL")
	if not lobby.is_empty():
		var uploader = load("res://scripts/match_statistics.gd").new()
		uploader.path = OS.get_environment("REPORT_TEST_DIR").path_join("real-outbox.json")
		uploader.sharing = func(): return true
		root.add_child(uploader)
		var game = load("res://scripts/game.gd").new()
		game._statistics_uploader = uploader
		game._statistics_context = {"id":OS.get_environment("REPORT_TEST_GAME"),"url":lobby,"token":OS.get_environment("REPORT_TEST_TOKEN")}
		game._record_finished_game(exported)
		check(uploader.pending.size() == 1, "actual game record path queued the native report")
		for i in 500:
			if uploader.pending.is_empty(): break
			await create_timer(0.01).timeout
		check(uploader.pending.is_empty() and uploader.requests_sent == 1, "real server acknowledged native statistics")
		check(game._statistics_context.is_empty(), "match credentials cleared after queuing")
		game._statistics_context = {"id":"stale"}
		game._game_kind = "skirmish"
		game._hub_begin({})
		check(game._statistics_context.is_empty(), "new offline game clears online match identity")
		game.free()
		uploader.free()
	print("STATISTICS EXPORT RESULT failures=", failures)
	quit(0 if failures == 0 else 1)

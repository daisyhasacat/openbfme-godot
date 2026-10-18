## DEVPORT-5 (stop S-4932): records a skirmish the local player loses through its own recorded MSG_SELF_DESTRUCT (RW 0x9218A4), so a replay of it
## reaches the recorder's end-game requests. tools/caster/test_caster_godot.py plays the replay through game.gd and checks the caster's report.
## The replay stays in the run's user data, never in the repository.
extends SceneTree

var failures := 0


func check(ok: bool, description: String) -> void:
	print("CASTER END ", "PASS " if ok else "FAIL ", description)
	if not ok:
		failures += 1


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("CASTER END SKIP: ROTWK_INSTALL and BFME2_INSTALL required")
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
	var path := OS.get_user_data_dir().path_join("caster-end.replay")
	check(world.start_new_game({"record": path, "logic_thread": false}).ok, "record fixture")
	for i in 5:
		world.advance(0.2)
	check(world.self_destruct(false).ok, "the recording player surrenders through MSG_SELF_DESTRUCT")
	var shown := false
	for i in 600:
		world.advance(0.2)
		for r in world.take_end_game_requests():
			shown = shown or r.kind == "show_end_game"
		if shown:
			break
	check(shown, "the recording player's own game requests its end screen")
	for i in 10:
		world.advance(0.2)
	world.clear_game_data(false)
	print("CASTER END fixture replay: ", path)
	print("CASTER END RESULT failures=", failures)
	quit(0 if failures == 0 else 1)

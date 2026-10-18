## CASTER-1: actual recording/playback through GameWorld and the caster's hotkey path.
## Keeps its generated replay in user data for rendered proof, never in the repository.
extends SceneTree

var failures := 0


func check(ok: bool, description: String) -> void:
	print("CASTER ", "PASS " if ok else "FAIL ", description)
	if not ok:
		failures += 1


func _initialize() -> void:
	call_deferred("run")


func check_paused_visibility(world: Node) -> void:
	var hidden := 0
	var exposed := 0
	for id in world.get_object_ids():
		var pose: Dictionary = world.get_render_pose(id)
		if pose.get("view_valid", false) and pose.presented_shrouded:
			hidden += 1
			if not pose.view_hidden:
				exposed += 1
	check(hidden > 0, "POV has shrouded rendered objects")
	check(exposed == 0, "paused POV immediately hides all %d shrouded rendered objects (%d exposed)" % [hidden, exposed])


func run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("CASTER SKIP: ROTWK_INSTALL and BFME2_INSTALL required")
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
	var setup: Dictionary = world.setup(fs)
	check(setup.ok, "setup")
	var slots: Array = []
	for i in 8:
		slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	slots[0] = {"state": 6, "name": "Westfold", "accepted": true, "color": 0, "start_pos": 0, "player_template": 3, "team": 0}
	slots[1] = {"state": 4, "name": "Mordor", "accepted": true, "color": 1, "start_pos": 1, "player_template": 6, "team": 1}
	slots[2] = {"state": 4, "name": "Rivendell", "accepted": true, "color": 2, "start_pos": 2, "player_template": 4, "team": 0}
	var prepared: Dictionary = world.prepare_new_game({"mode": "skirmish", "difficulty": 1, "map": "maps/map mp fall back 4p/map mp fall back 4p.map",
		"map_crc": 0, "map_size": 0, "map_mask": 0, "seed": 777, "starting_cash": 1500, "slots": slots})
	check(prepared.ok, "prepare fixture")
	if not prepared.ok:
		print(prepared)
		quit(1)
		return
	var path := OS.get_user_data_dir().path_join("caster-vision.replay")
	var started: Dictionary = world.start_new_game({"record": path, "logic_thread": false})
	check(started.ok, "record fixture")
	check(not world.caster_status().active, "active player has no caster UI")
	check(not world.caster_metrics().active, "active player cannot read broadcast statistics")
	check(not world.caster_observe(-1), "active player cannot reveal map")
	check(not world.caster_playback(true, 2.0), "active player cannot use caster transport")
	check(not world.caster_step().ok, "active player cannot step via caster controls")
	var chosen := false
	for object in world.get_player_objects(started.start.local_player):
		if not object.structure:
			check(world.order_stop([object.id]).ok, "record a real selection command")
			chosen = true
			break
	check(chosen, "recording player has a starting object")
	var hashes: Array = []
	var frame_count := 6000 if OS.get_cmdline_user_args().has("--measure-20") else 50
	for i in frame_count:
		world.advance(0.2)
		hashes.append(world.get_state_hash())
	world.clear_game_data(false)
	check(world.prepare_replay(path).ok, "prepare recorded replay")
	check(world.start_replay({"logic_thread": false}).ok, "start observer replay")
	var caster = load("res://scripts/caster_hud.gd").new()
	root.add_child(caster)
	caster.configure(world, null)
	check(caster.player_buttons.size() == 3, "three actual occupied player slots")
	check(caster.status_label.text.contains("no recorded camera"), "camera-track absence is visible")
	check(world.caster_playback(true, 1.0), "pause before changing vision")
	var initial: int = world.get_state_hash()
	for key in [KEY_1, KEY_2, KEY_3]:
		var event := InputEventKey.new()
		event.keycode = key
		event.pressed = true
		check(caster.handle_input(event), "POV hotkey handled")
		var state: Dictionary = world.caster_status()
		var slot: int = key - KEY_1
		check(state.observed.index == state.players[slot].index, "hotkey selects the slot's actual player index")
		check(world.get_state_hash() == initial, "POV change preserves hash")
		check_paused_visibility(world)
	caster.select_player(0)
	var team_key := InputEventKey.new()
	team_key.keycode = KEY_T
	team_key.pressed = true
	caster.handle_input(team_key)
	check(world.caster_status().team, "team vision via hotkey")
	check_paused_visibility(world)
	caster.select_all()
	check(world.caster_status().observed.all, "all vision restored")
	check(not world.caster_observe(255), "invalid player rejected")
	check(world.caster_playback(false, 1.0), "resume after paused vision checks")
	for i in frame_count:
		caster.select_player(i % 3)
		world.advance(0.2)
		check(world.get_state_hash() == hashes[i], "frame %d matches straight recording" % (i + 1))
	var status: Dictionary = world.replay_status()
	check(status.mismatches == 0, "recorded CRCs all match")
	check(status.hashes_compared == frame_count, "every recorded frame checked")
	check(world.caster_playback(true, 0.25), "quarter-speed accepted")
	check(world.caster_playback(true, 8.0), "eight-speed accepted")
	check(not world.caster_playback(false, 0.0), "invalid speed rejected")
	check(not world.caster_step().ok, "cannot step past replay end")
	check(not world.replay_seek_step(25, 1).ok, "backward seek requires a restart, not a fake snapshot")
	caster.free()
	world.clear_game_data(false)
	var started_ms := Time.get_ticks_msec()
	check(world.prepare_replay(path).ok, "rewind prepares original replay")
	check(world.start_replay({"logic_thread": true}).ok, "rewind reconstructs initial world")
	var target := frame_count + 1 if frame_count == 6000 else 26
	while true:
		var seek: Dictionary = world.replay_seek_step(target, 20)
		check(seek.ok, "seek budget")
		if not seek.ok or seek.ready:
			break
		await process_frame
	print("CASTER SEEK MEASUREMENT ", JSON.stringify({"target": target, "game_seconds": (target - 1) / 5.0,
		"elapsed_ms_including_load": Time.get_ticks_msec() - started_ms, "status": world.replay_status()}))
	check(world.get_state_hash() == hashes[target - 2], "seek hash equals uninterrupted recording")
	check(world.replay_status().hashes_compared == target - 1, "seek compares every intervening hash")
	if target < frame_count + 1:
		check(world.caster_step().ok, "single step after seek")
		check(world.replay_status().frame == target + 1, "step advances exactly one frame")
		check(world.get_state_hash() == hashes[target - 1], "step hash equals uninterrupted recording")
	print("CASTER fixture replay: ", path)
	print("CASTER RESULT failures=", failures)
	quit(0 if failures == 0 else 1)

## Run in two separate processes with RESUME_TEST_DIR and RESUME_TEST_PHASE=capture|restore.
extends SceneTree
class Flow extends RefCounted:
	var game
	var last_load := {}
	func load_save(path: String) -> void:
		var prep: Dictionary = game._world.prepare_save_load(path)
		assert(prep.ok, str(prep))
		var start: Dictionary = game._world.start_save_load({"logic_thread": false})
		assert(start.ok, str(start))
		last_load = game._world.save_load_step(100)
		assert(last_load.state == "ready", str(last_load))
		game._state = game.State.PLAYING
class Host extends Node:
	enum State { MENU, PLAYING }
	var _state := State.MENU
	var _world: Node
	var _save_flow := Flow.new()
	var _net_state := ""
	var _replay_active := false
	var _online_spectate := false
	var _game_kind := "skirmish"
	var _benchmark_tool = null
	var _campaign_flow = null
	var _hud = null
	var _lua_console = null
	var _quit_open := false
	func _toggle_quit_menu() -> void: _quit_open = true
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var folder := OS.get_environment("RESUME_TEST_DIR")
	var phase := OS.get_environment("RESUME_TEST_PHASE")
	assert(not folder.is_empty() and phase in ["capture", "restore"])
	assert(DirAccess.make_dir_recursive_absolute(folder) == OK)
	var fs = ClassDB.instantiate("RetailFileSystem"); assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld"); root.add_child(world); assert(world.setup(fs).ok)
	var host := Host.new(); host._world = world; host._save_flow.game = host; root.add_child(host)
	var service = load("res://scripts/ios_quick_resume.gd").new()
	service.game = host; service.enabled = true; service.directory = folder
	root.add_child(service); service.set_process(false)
	if phase == "capture":
		var slots := []
		for i in 8: slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
		for i in 2: slots[i] = {"state": 6, "name": "Resume test %d" % i, "accepted": true, "color": i, "start_pos": i, "player_template": 4, "team": i}
		assert(world.prepare_new_game({"mode": "skirmish", "map_crc": 0, "map_size": 0, "map": "maps/map wor buckland/map wor buckland.map", "seed": 42, "starting_cash": 2000, "slots": slots}).ok)
		assert(world.start_new_game({"logic_thread": false, "record": folder.path_join("record.BfME2Replay")}).ok)
		host._state = Host.State.PLAYING
		for i in 5: world.advance(0.2)
		world.set_paused(true)
		service.notification(Node.NOTIFICATION_APPLICATION_PAUSED)
		assert(service.last_result.ok, str(service.last_result))
		assert(world.is_paused())
	else:
		var state: Dictionary = service._read("current.state")
		assert(await service.try_restore())
		assert(world.get_frame() == state.frame)
		assert(world.get_state_hash() == state.hash)
		assert(world.is_paused())
		assert(not FileAccess.file_exists(service._path("attempt.state")))
		service.discard()
	service.free(); host.free(); world.free()
	print("IOS QUICK RESUME NATIVE %s PASS" % phase.to_upper())
	quit()

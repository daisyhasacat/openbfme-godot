## Real recorded match: the host API writes a directly loadable snapshot, without a shell.
extends SceneTree
class Host extends Node:
	enum State { PLAYING }
	var _state := State.PLAYING
	var _campaign_flow = null
	var _world: Node
class Flow extends "res://scripts/save_flow.gd":
	var folder: String
	func save_dir() -> String: return folder
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem"); assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld"); root.add_child(world); assert(world.setup(fs).ok)
	var temporary := OS.get_environment("TMPDIR")
	if temporary.is_empty(): temporary = OS.get_user_data_dir()
	var folder := temporary.path_join("openbfme-direct-save-%d" % OS.get_process_id())
	assert(DirAccess.make_dir_recursive_absolute(folder) == OK)
	var slots := []
	for i in 8: slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	for i in 2: slots[i] = {"state": 6, "name": "Save test %d" % i, "accepted": true, "color": i, "start_pos": i, "player_template": 4, "team": i}
	assert(world.prepare_new_game({"mode": "skirmish", "map_crc": 0, "map_size": 0, "map": "maps/map wor buckland/map wor buckland.map", "seed": 42, "starting_cash": 2000, "slots": slots}).ok)
	assert(world.start_new_game({"logic_thread": false, "record": folder.path_join("record.BfME2Replay")}).ok)
	for i in 5: world.advance(0.2)
	world.set_paused(true)
	var host := Host.new(); host._world = world; root.add_child(host)
	var flow := Flow.new(host); flow.folder = folder
	var saved := flow.save_game("Fortress test")
	assert(saved.ok, str(saved))
	assert(world.is_paused())
	var frame: int = world.get_frame()
	var hash_before: int = world.get_state_hash()
	assert(saved.frame == frame and saved.hash == hash_before)
	assert(world.check_save(saved.path).ok)
	assert(world.prepare_save_load(saved.path).ok)
	assert(world.start_save_load({"logic_thread": false}).ok)
	var loaded: Dictionary = world.save_load_step(100)
	assert(loaded.state == "ready", str(loaded))
	assert(world.get_frame() == frame and world.get_state_hash() == hash_before)
	world.free(); host.free()
	for file in DirAccess.get_files_at(folder): DirAccess.remove_absolute(folder.path_join(file))
	DirAccess.remove_absolute(folder)
	print("DIRECT SAVE NATIVE PASS")
	quit()

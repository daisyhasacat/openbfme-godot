extends SceneTree
const Resume := preload("res://scripts/ios_quick_resume.gd")
class PublishProbe extends "res://scripts/ios_quick_resume.gd":
	var refuse_publish := false
	func _write(name: String, state: Dictionary) -> Error:
		if refuse_publish and name == "current.state": return ERR_CANT_CREATE
		return super._write(name, state)
class World extends RefCounted:
	var frame := 10
	var writes := 0
	var paused := false
	var refuse := false
	func get_frame() -> int: return frame
	func get_state_hash() -> int: return frame
	func is_paused() -> bool: return paused
	func set_paused(value: bool) -> void: paused = value
	func save_game(path: String, _options: Dictionary) -> Dictionary:
		writes += 1
		if refuse: return {"ok": false, "error": "Unsupported snapshot"}
		var file := FileAccess.open(path, FileAccess.WRITE)
		file.store_string(str(frame)); file.close()
		return {"ok": true, "frame": frame, "hash": frame, "path": path}
	func check_save(path: String) -> Dictionary:
		return {"ok": FileAccess.file_exists(path)}
class Flow extends RefCounted:
	var game
	var last_load := {}
	var loads := 0
	var fail := false
	func load_save(path: String) -> void:
		loads += 1
		if fail: return
		game._world.frame = int(FileAccess.get_file_as_string(path))
		game._state = game.State.PLAYING
		last_load = {"state": "ready"}
class Game extends Node:
	enum State { MENU, PLAYING }
	var _state := State.PLAYING
	var _world := World.new()
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
var folder: String
func _initialize() -> void: call_deferred("_run")
func manager(game: Node) -> Node:
	var service := PublishProbe.new(); service.game = game; service.enabled = true; service.directory = folder
	root.add_child(service); service.set_process(false)
	return service
func _run() -> void:
	var base := OS.get_environment("TMPDIR")
	if base.is_empty(): base = OS.get_user_data_dir()
	folder = base.path_join("openbfme-quick-resume-%d" % OS.get_process_id())
	var game := Game.new(); game._save_flow.game = game; root.add_child(game)
	var service = manager(game)
	service._process(29.0); assert(game._world.writes == 0)
	service._process(1.0); assert(game._world.writes == 1)
	assert(service._read("current.state").slot == "a")
	var first: Dictionary = service._read("current.state")
	game._world.frame = 11
	game._world.paused = true
	service._notification(Node.NOTIFICATION_APPLICATION_PAUSED)
	assert(game._world.writes == 2 and game._world.paused)
	service._notification(Node.NOTIFICATION_APPLICATION_PAUSED)
	assert(game._world.writes == 2) # Duplicate lifecycle delivery is deduplicated.
	assert(service._read("current.state").slot == "b")
	service._process(60.0); assert(game._world.writes == 2)
	service._notification(Node.NOTIFICATION_APPLICATION_RESUMED)
	assert(game._save_flow.loads == 0) # A still-running app never reloads itself.
	game._world.refuse = true; game._world.frame = 12
	assert(not service.capture().ok)
	assert(service._read("current.state").frame == 11)
	assert(FileAccess.get_file_as_string(service._path("a.obfmesave")) == "10")
	game._world.refuse = false; game._world.frame = 13
	service.refuse_publish = true
	assert(not service.capture().ok)
	assert(service._read("current.state").frame == 11)
	assert(FileAccess.get_file_as_string(service._path("b.obfmesave")) == "11")
	service.free()
	# A new service reads persisted metadata, including paused state, after restart.
	game._state = Game.State.MENU; game._world.frame = 0; game._world.paused = false
	service = manager(game)
	assert(await service.try_restore())
	assert(game._world.frame == 11 and game._world.paused)
	assert(game._save_flow.loads == 1)
	assert(not await service.try_restore())
	service.free()
	# Failed restores are never retried on the next launch.
	game._state = Game.State.MENU; game._save_flow.fail = true
	service = manager(game)
	assert(not await service.try_restore())
	assert(game._save_flow.loads == 2)
	service.free(); service = manager(game)
	assert(not await service.try_restore())
	assert(game._save_flow.loads == 2)
	game._state = Game.State.PLAYING
	game._net_state = "online"
	assert(not service.capture().ok)
	game._net_state = ""; game._replay_active = true
	assert(not service.capture().ok)
	service.discard()
	assert(not FileAccess.file_exists(service._path("current.state")))
	for file in DirAccess.get_files_at(folder): DirAccess.remove_absolute(folder.path_join(file))
	DirAccess.remove_absolute(folder)
	service.free(); game.free()
	print("IOS QUICK RESUME PASS")
	quit()

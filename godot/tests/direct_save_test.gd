extends SceneTree
class World extends RefCounted:
	var calls: Array = []
	var refusal := ""
	func save_game(path: String, options: Dictionary) -> Dictionary:
		calls.append([path, options])
		if not refusal.is_empty(): return {"ok": false, "error": refusal}
		return {"ok": true, "path": path, "frame": 42}
class Campaign extends RefCounted:
	var active := true
class Game extends Node:
	enum State { MENU, PLAYING }
	var _state := State.PLAYING
	var _campaign_flow = null
	var _world := World.new()
class Flow extends "res://scripts/save_flow.gd":
	func save_dir() -> String: return "/tmp/openbfme-direct-save-test"
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var game := Game.new(); root.add_child(game)
	var flow := Flow.new(game)
	assert(flow.save_game().ok)
	assert(game._world.calls == [["/tmp/openbfme-direct-save-test/Quick Save.obfmesave", {"description": "Quick Save", "kind": 2}]])
	for name in ["", "..", "../outside", "folder/file", "bad:name", "a".repeat(101)]:
		assert(not flow.save_game(name).ok)
	game._state = Game.State.MENU
	assert(not flow.save_game("No match").ok)
	assert(game._world.calls.size() == 1)
	var console = load("res://scripts/lua_console.lua").new()
	console.history_path = "/tmp/openbfme-direct-save-console-%d.cfg" % OS.get_process_id()
	console.execute = func(_code): return {"ok": false, "output": "Should not execute Lua"}
	console.save_game = func(name: String): return flow.save_game(name)
	root.add_child(console)
	game._state = Game.State.PLAYING
	console._submit("/save Fortress test")
	assert(console._output.text.contains("Saved Fortress test at frame 42"))
	assert(game._world.calls.back()[1].description == "Fortress test")
	console._submit("/save")
	assert(game._world.calls.back()[1].description == "Quick Save")
	console._submit("/save ../escape")
	assert(console._output.text.contains("without path separators"))
	assert(game._world.calls.size() == 3)
	game._campaign_flow = Campaign.new()
	assert(flow.save_game("Mission").ok)
	assert(game._world.calls.back()[1].kind == 1)
	game._world.refusal = "Cannot save a network game"
	console._submit("/save Network")
	assert(console._output.text.contains("Cannot save a network game"))
	assert(not flow.last_save.ok)
	DirAccess.remove_absolute(console.history_path)
	console.free(); game.free()
	print("DIRECT SAVE PASS")
	quit()

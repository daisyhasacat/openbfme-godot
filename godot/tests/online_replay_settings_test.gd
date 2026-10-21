## A network recording must restore the same simulation settings, including the absence of offline starting-wall jobs.
extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	create_timer(20.0).timeout.connect(func(): quit(1))
	var path := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--record="):
			path = arg.trim_prefix("--record=")
	assert(not path.is_empty())
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world: Node = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var start := {"v": 1, "kind": "openbfme-start", "game": "0123456789abcdef", "profile": world.net_profile_digest(),
		"slot": 0, "relay": "127.0.0.1:1", "ticket": "AQHvzauJZ0UjAQABAJQ1d4PB9MrcgEkQq2vALWMZEck", "runAhead": 1, "crcInterval": 100,
		"setup": {"map": "maps/map mp fords of isen ii/map mp fords of isen ii.map", "seed": 777, "startingCash": 1500,
		"superweaponRestriction": 0, "legendaryHeroes": 1, "rules": null, "luaAuthoritySlot": 0,
		"slots": [{"state": "player", "name": "Recorder", "faction": 3, "color": 0, "startPos": 0, "team": 0},
		{"state": "hard", "name": "Computer", "faction": 6, "color": 1, "startPos": 1, "team": 1}]}}
	# This fixture needs no relay: net_begin writes its recording header before any network frames run.
	assert(world.net_online(JSON.stringify(start)).ok)
	var message: Dictionary = world.net_poll().message
	assert(message.lua_authority_slot == 0)
	for invalid in [-2, 8, 1, "0"]:
		var bad := message.duplicate(true)
		bad.lua_authority_slot = invalid
		assert(world.prepare_new_game(bad).ok == false)
	var prepared: Dictionary = world.prepare_new_game(message)
	assert(prepared.ok)
	assert(prepared.resolved.lua_authority_slot == 0)
	assert(world.start_new_game({"logic_thread": false}).ok)
	var recorded_hash: int = world.get_state_hash()
	assert(world.net_begin({"record": path}).ok)
	world.clear_game_data(false)
	assert(world.prepare_replay(path).ok)
	assert(world.start_replay({"logic_thread": false}).ok)
	print("REPLAY SETTINGS hashes: recorded=", recorded_hash, " restored=", world.get_state_hash())
	assert(world.get_state_hash() == recorded_hash, "Network replay changed the initial simulation settings")
	print("ONLINE REPLAY SETTINGS PASS: initial state hash preserved")
	quit()

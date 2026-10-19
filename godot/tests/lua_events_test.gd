extends SceneTree
# The bfme.* engine handlers in a live game, and that scripting does not change the simulation (docs/BFME_LUA.md).
# Needs ROTWK_INSTALL / BFME2_INSTALL.

const MAP := "map mp fall back 4p"

func _initialize() -> void:
	call_deferred("_run")

func _game() -> Array:
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var report: Dictionary = world.load_map(MAP, {"slots": [{"player": "Player_1", "faction": "FactionMen", "human": true}], "seed": 7})
	assert(report.ok, str(report))
	world.set_logic_thread(false)
	var owner := -1
	for i in report.players.size():
		if String(report.players[i]).contains("'Player_1'"): owner = i
	assert(owner != -1, str(report.players))
	return [world, owner]

func _lua(world, code: String) -> String:
	var r: Dictionary = world.execute_lua(code)
	assert(r.ok, code + "\n" + str(r.output))
	return str(r.output)

func _steps(world, count: int, flush: bool) -> void:
	for i in count:
		world.advance(0.2)
		if flush:
			world.update_lua()

func _run() -> void:
	# --- the same game twice: handlers and per-frame flushes change nothing the simulation hashes
	var plain: Array = _game()
	var world_a = plain[0]
	var owner_a: int = plain[1]
	world_a.create_object("GondorBarracks", owner_a, 2350.0, 2500.0, 0.0)
	_steps(world_a, 150, false)
	var hash_plain: int = world_a.get_state_hash()
	world_a.queue_free()
	await process_frame
	var scripted: Array = _game()
	var world = scripted[0]
	var owner: int = scripted[1]
	_lua(world, """
		seen = {}
		for _, event in ipairs(bfme.events.engineEvents()) do
			bfme.events.on(event, "test", function(e) seen[event] = (seen[event] or 0) + 1; last = last or {}; last[event] = e; if event == "unitTrained" then trained = trained or {}; trained[#trained+1] = e.typeId end end)
		end
	""")
	world.create_object("GondorBarracks", owner, 2350.0, 2500.0, 0.0)
	_steps(world, 150, true)
	var hash_scripted: int = world.get_state_hash()
	assert(hash_plain == hash_scripted, "registering handlers changed the game: %d vs %d" % [hash_plain, hash_scripted])
	assert(_lua(world, "return seen.matchStarted") == "1\n")

	# --- the engine handlers fire from the game's own events
	var porter: int = world.create_object("MenPorter", owner, 1800.0, 1800.0, 0.0)
	var barracks: int = world.create_object("GondorBarracks", owner, 2200.0, 1800.0, 0.0)
	# tolerance: create_object returns the new object's ID (any positive value) or 0 on failure
	assert(porter > 0 and barracks > 0)
	world.update_lua()
	_lua(world, "bfme.players.localPlayer.money = 20000")
	world.update_lua()
	var site := ""
	for spot in ["{x=1500,y=1500}", "{x=1550,y=2100}", "{x=1350,y=1800}", "{x=1700,y=1300}"]:
		var placed: String = _lua(world, "return bfme.players.localPlayer:canBuildAt('GondorFarm', %s, bfme.unitInstances[%d])" % [spot, porter])
		if placed.begins_with("true"):
			site = spot
			break
	assert(not site.is_empty(), "no legal farm site for the porter")
	_lua(world, "farm_ticket = bfme.unitInstances[%d]:construct('GondorFarm', %s)" % [porter, site])
	_lua(world, "train_ticket = bfme.buildingInstances[%d]:trainUnit('GondorFighterHorde')" % barracks)
	for i in 900:
		world.advance(0.2)
		world.update_lua()
		if _lua(world, "return (seen.constructionComplete or 0) > 0 and (seen.unitTrained or 0) > 0") == "true\n":
			break
	assert(_lua(world, "return seen.buildingPlaced or 0") != "0\n", _lua(world, "return bfme.mutations.getStatus(farm_ticket)"))
	assert(_lua(world, "return last.buildingPlaced.typeId, last.buildingPlaced.builderId == %d" % porter) == "GondorFarm\ttrue\n")
	assert(_lua(world, "return (seen.constructionComplete or 0) > 0") == "true\n", _lua(world, "return bfme.mutations.getStatus(farm_ticket)"))
	assert(_lua(world, "return (seen.unitTrained or 0) > 0, last.unitTrained.producerId == %d" % barracks) == "true\ttrue\n",
		_lua(world, "return bfme.mutations.getStatus(train_ticket)"))
	# the building produces the horde, then each of its members: one event per object
	assert(_lua(world, "return trained[1], trained[2]") == "GondorFighterHorde\tGondorFighter\n", _lua(world, "return table.concat(trained, ',')"))
	# a death: the porter is killed through the API (a script's explicit action)
	_lua(world, "bfme.unitInstances[%d]:kill()" % porter)
	_steps(world, 10, true)
	assert(_lua(world, "return last.objectDied ~= nil and last.objectDied.objectId == %d" % porter) == "true\n", _lua(world, "return seen.objectDied"))
	# the match end
	world.clear_game_data(false)
	world.update_lua()
	assert(_lua(world, "return seen.matchEnded") == "1\n")
	print("LUA EVENTS PASS: ", _lua(world, "local out = {}; for k, v in pairs(seen) do out[#out + 1] = k .. '=' .. v end; table.sort(out); return table.concat(out, ' ')").strip_edges())
	world.queue_free()
	await process_frame
	quit(0)

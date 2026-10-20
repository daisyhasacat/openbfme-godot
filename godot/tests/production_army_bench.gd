# Opt-in installed-data benchmark: production timing hooks beside a large army.
# PRODUCTION_HORDES defaults to 667 (40,020 CP with 60-CP battalions).
# PRODUCTION_HASHES optionally records every frame for before/after comparison.
extends SceneTree
var world: Node
func _initialize() -> void: call_deferred("_run")
func lua(code: String) -> String:
	var result: Dictionary = world.execute_lua(code)
	assert(result.ok, str(result))
	return String(result.output).strip_edges()
func _run() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var slots: Array = []
	for i in 8: slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	slots[0] = {"state":6,"name":"Production benchmark","accepted":true,"color":0,"start_pos":3,"player_template":4,"team":0}
	assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map":"maps/buckland snow/buckland snow.map","map_crc":0,"map_size":0,"map_mask":0,"seed":4711,"starting_cash":100000,"slots":slots}).ok)
	assert(world.start_new_game({"logic_thread":false}).ok)
	var owner := int(lua("local p=bfme.players.localPlayer; p.money=1000000; p.commandPointsBase=1000000; p.commandPointsCap=1000000; return p.id"))
	world.update_lua()
	var count := 667
	if not OS.get_environment("PRODUCTION_HORDES").is_empty(): count = int(OS.get_environment("PRODUCTION_HORDES"))
	for i in count:
		var id: int = world.create_object("GondorFighterHorde", owner, 1700.0 + float(i % 19) * 50.0, 450.0 + float(i / 19) * 50.0, 0)
		# tolerance: zero is create_object's failure sentinel; every requested battalion must exist.
		assert(id != 0)
	var producers: Array[int] = []
	for i in 8:
		var id: int = world.create_object("GondorBarracks", owner, 1500.0 + float(i % 4) * 250.0, 2600.0 + float(i / 4) * 300.0, 0)
		# tolerance: zero is create_object's failure sentinel; all eight producers must exist.
		assert(id != 0)
		producers.append(id)
	for i in 30: world.advance(0.2)
	# Same context-only timing function used by Ubermode. No wall-clock update
	# callbacks: snapshots must not affect the measured simulation or hashes.
	lua("bfme.timing.onTrain(function(c) if c.playerId==" + str(owner) + " then return 1 end end)")
	for id in producers: lua("bfme.buildingInstances[" + str(id) + "]:trainUnit('GondorFighterHorde',5)")
	world.update_lua()
	print("PRODUCTION ARMY READY hordes=", count, " CP=", lua("return bfme.players.localPlayer.commandPointsUsed"))
	var times: Array[float] = []
	var hashes: Array = []
	for i in 40:
		var start := Time.get_ticks_usec()
		world.advance(0.2)
		times.append((Time.get_ticks_usec() - start) / 1000.0)
		hashes.append(world.get_state_hash())
	var total := 0.0
	for value in times: total += value
	var first := times[0]
	times.sort()
	print("PRODUCTION ARMY advance mean=", total / times.size(), " first=", first, " p95=", times[38], " max=", times.back())
	var path := OS.get_environment("PRODUCTION_HASHES")
	if not path.is_empty():
		var file := FileAccess.open(path, FileAccess.WRITE)
		assert(file != null)
		file.store_string(JSON.stringify(hashes))
	print("PRODUCTION ARMY COMPLETE objects=", world.get_object_count())
	world.free()
	quit()

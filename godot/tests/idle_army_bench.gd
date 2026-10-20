# Opt-in idle army benchmark: simulation, Lua callbacks and rendered client poses.
# IDLE_HORDES defaults to 667 (40,020 CP with 60-CP battalions).
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
	slots[0] = {"state":6,"name":"Idle army benchmark","accepted":true,"color":0,"start_pos":3,"player_template":4,"team":0}
	assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map":"maps/buckland snow/buckland snow.map","map_crc":0,"map_size":0,"map_mask":0,"seed":4711,"starting_cash":100000,"slots":slots}).ok)
	assert(world.start_new_game({"logic_thread":false}).ok)
	var owner := int(lua("local p=bfme.players.localPlayer; p.money=1000000; p.commandPointsBase=1000000; p.commandPointsCap=1000000; return p.id"))
	world.update_lua()
	var count := 667
	if not OS.get_environment("IDLE_HORDES").is_empty(): count = int(OS.get_environment("IDLE_HORDES"))
	for i in count:
		var id: int = world.create_object("GondorFighterHorde", owner, 1700.0 + float(i % 19) * 50.0, 450.0 + float(i / 19) * 50.0, 0)
		# tolerance: zero is create_object's failure sentinel; every requested battalion must exist.
		assert(id != 0)
	for i in 30: world.advance(0.2)
	lua("bfme.console.reload()")
	if OS.get_environment("IDLE_UBER") != "0": lua("ubermode.enable()")
	world.update_lua()
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.position = Vector3(2100, 2200, -1300)
	camera.look_at(Vector3(2100, 0, -1300), Vector3(0, 0, -1))
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 2500
	camera.current = true
	for i in 5: await process_frame
	print("IDLE ARMY READY CP=", lua("return bfme.players.localPlayer.commandPointsUsed"))
	var advance_times: Array[float] = []
	var lua_times: Array[float] = []
	var frame_times: Array[float] = []
	var frames := 120
	if not OS.get_environment("IDLE_FRAMES").is_empty(): frames = int(OS.get_environment("IDLE_FRAMES"))
	var hashes: Array = []
	for i in frames:
		var start := Time.get_ticks_usec()
		world.advance(0.2)
		advance_times.append((Time.get_ticks_usec() - start) / 1000.0)
		var before_lua := Time.get_ticks_usec()
		world.update_lua()
		lua_times.append((Time.get_ticks_usec() - before_lua) / 1000.0)
		await process_frame
		frame_times.append((Time.get_ticks_usec() - start) / 1000.0)
		if not OS.get_environment("IDLE_HASHES").is_empty(): hashes.append(world.get_state_hash())
	if not OS.get_environment("IDLE_HASHES").is_empty():
		var file := FileAccess.open(OS.get_environment("IDLE_HASHES"), FileAccess.WRITE)
		assert(file != null)
		file.store_string(JSON.stringify(hashes))
	_report("advance", advance_times)
	_report("lua", lua_times)
	_report("frame", frame_times)
	print("IDLE ARMY COMPLETE objects=", world.get_object_count())
	camera.free()
	world.free()
	quit()
func _report(label: String, values: Array[float]) -> void:
	var total := 0.0
	for value in values: total += value
	values.sort()
	print("IDLE ", label, " mean=", total / values.size(), " p95=", values[int(values.size() * 0.95)], " max=", values.back())

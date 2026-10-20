# Opt-in installed-data benchmark. Reads the named user layout without modifying it.
# BUCKLAND_LAYOUT defaults to snowBuck; no user layout or game assets are committed.
extends SceneTree
var world: Node
func lua(code: String) -> String:
	var result: Dictionary = world.execute_lua(code)
	assert(result.ok, str(result))
	return String(result.output).strip_edges()
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var layout := OS.get_environment("BUCKLAND_LAYOUT")
	if layout.is_empty(): layout = "snowBuck"
	var path := "user://building_layouts/" + layout + ".json"
	if not FileAccess.file_exists(path):
		print("SKIP: named building layout is unavailable")
		quit(77)
		return
	var data: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path))
	var start_slot := int(data.buildings[0].sourceStartSlot)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var slots: Array = []
	for i in 8: slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	slots[0] = {"state":6,"name":"Army benchmark","accepted":true,"color":0,"start_pos":start_slot,"player_template":4,"team":0}
	assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map":"maps/buckland snow/buckland snow.map","map_crc":0,"map_size":0,"map_mask":0,"seed":4711,"starting_cash":100000,"slots":slots}).ok)
	assert(world.start_new_game({"logic_thread":false}).ok)
	lua("bfme.console.reload(); ubermode.enable(); replayBuildings(" + JSON.stringify(layout) + ", {level3=true})")
	for i in 120:
		world.advance(0.2)
		world.update_lua()
		if i % 10 == 0:
			var status := lua("return buildingReplay.running,buildingReplay.completed,buildingReplay.failed")
			print("BUCKLAND REPLAY ", status)
			if status.begins_with("false"):
				break
		await create_timer(0.051).timeout
	print("BUCKLAND REPLAY STATE ", lua("return buildingReplay.running,buildingReplay.completed,buildingReplay.failed"))
	# Retain pending layout callbacks, matching the live console workload.
	var owner := int(lua("return bfme.players.localPlayer.id"))
	var count := 334
	if not OS.get_environment("BUCKLAND_HORDES").is_empty(): count = int(OS.get_environment("BUCKLAND_HORDES"))
	var ids: Array = []
	for i in count:
		var id: int = world.create_object("GondorFighterHorde", owner, 1750.0 + float(i % 19) * 50.0, 450.0 + float(i / 19) * 50.0, 0)
		# tolerance: create_object returns zero on failure; every requested horde must exist.
		assert(id != 0)
		ids.append(id)
	for i in 30: world.advance(0.2)
	world.update_lua()
	assert(world.order_move(ids, 900, 2600, {}).ok)
	var frame_count := 120
	if not OS.get_environment("BUCKLAND_FRAMES").is_empty(): frame_count = int(OS.get_environment("BUCKLAND_FRAMES"))
	print("BUCKLAND ARMY READY ", count, " hordes, ", data.buildings.size(), " recorded buildings")
	var advance_ms: Array[float] = []
	var lua_ms: Array[float] = []
	var hashes: Array = []
	for i in frame_count:
		var start := Time.get_ticks_usec()
		world.advance(0.2)
		advance_ms.append((Time.get_ticks_usec() - start) / 1000.0)
		start = Time.get_ticks_usec()
		world.update_lua()
		lua_ms.append((Time.get_ticks_usec() - start) / 1000.0)
		hashes.append(world.get_state_hash())
		if i in [0, 1, 4, 9, 19]:
			var ready := 0
			var waiting := 0
			var retrying := 0
			var moving := 0
			for id in ids:
				var state: Dictionary = world.get_object(id)
				ready += int(state.get("ai_has_path", false))
				waiting += int(state.get("ai_waiting_for_path", false))
				retrying += int(int(state.get("ai_queue_for_path_frame", 0)) != 0)
				moving += int(state.get("ai_moving", false))
			print("BUCKLAND RESPONSE tick=", i + 1, " paths=", ready, " waiting=", waiting, " retrying=", retrying, " moving=", moving, "/", ids.size())
		if i == 0:
			var goals: Dictionary = {}
			var bounds := Rect2()
			var active := 0
			for id in ids:
				var object: Dictionary = world.get_object(id)
				if object.has("ai_goal"):
					var goal: Vector2 = object.ai_goal
					if goal == Vector2.ZERO: continue # idle/failed move states have no destination
					active += 1
					if goals.is_empty(): bounds = Rect2(goal, Vector2.ZERO)
					else: bounds = bounds.expand(goal)
					goals[goal] = true
			print("BUCKLAND DESTINATIONS unique=", goals.size(), " active=", active, "/", ids.size(), " bounds=", bounds)
	_report("advance", advance_ms)
	_report("lua", lua_ms)
	var out := OS.get_environment("BUCKLAND_HASHES")
	if not out.is_empty():
		var file := FileAccess.open(out, FileAccess.WRITE)
		assert(file != null)
		file.store_string(JSON.stringify(hashes))
	print("BUCKLAND BENCH COMPLETE: frames ", frame_count, " objects ", world.get_object_count())
	world.free()
	quit()
func _report(label: String, values: Array[float]) -> void:
	var total := 0.0
	for value in values: total += value
	values.sort()
	print("BUCKLAND ", label, " mean=", total / values.size(), " p95=", values[int(values.size() * 0.95)], " max=", values.back())

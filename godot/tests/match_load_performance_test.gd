## Measures real map loading for a new skirmish and an exact saved-game restore.
## Prints wall time and engine stages; assertions check state, not machine-specific timing.
extends SceneTree

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	create_timer(180).timeout.connect(func(): quit(1))
	var folder := OS.get_environment("LOAD_PERF_DIR")
	assert(not folder.is_empty(), "Set LOAD_PERF_DIR to a temporary output directory")
	assert(DirAccess.make_dir_recursive_absolute(folder) == OK)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var slots := []
	for i in 8: slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	for i in 2: slots[i] = {"state":6,"name":"Load test %d" % i,"accepted":true,"color":i,"start_pos":i,"player_template":4,"team":i}
	var map := OS.get_environment("LOAD_PERF_MAP")
	if map.is_empty(): map = "map wor buckland"
	assert(world.prepare_new_game({"mode":"skirmish","map_crc":0,"map_size":0,"map":"maps/%s/%s.map" % [map,map],"seed":42,"starting_cash":2000,"slots":slots}).ok)
	var started := Time.get_ticks_usec()
	var report: Dictionary = world.start_new_game({"logic_thread":false,"record":folder.path_join("load.BfME2Replay")})
	assert(report.ok,str(report))
	var new_wall_ms := (Time.get_ticks_usec()-started)/1000.0
	assert(report.terrain.reused_gameplay_map)
	var new_terrain: Dictionary = report.terrain
	var new_hash: int = world.get_state_hash()
	# The standalone viewer still parses independently; rendering must leave the gameplay map untouched.
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var terrain = builder.build_map(fs,map,{"markers":false,"fog":false})
	assert(terrain != null)
	var standalone: Dictionary = builder.get_report()
	assert(not standalone.reused_gameplay_map)
	assert(new_terrain.mesh == standalone.mesh)
	assert(new_terrain.atlas == standalone.atlas)
	assert(new_terrain.errors == standalone.errors)
	assert(world.get_state_hash() == new_hash)
	terrain.free()
	print("LOAD PERF new ", JSON.stringify({"wall_ms":new_wall_ms,"stages":report.timings_ms,"terrain":report.terrain.timings_ms}))
	for i in 5: world.advance(0.2)
	var frame: int = world.get_frame()
	var hash_before: int = world.get_state_hash()
	var save := folder.path_join("load.obfmesave")
	assert(world.save_game(save,{"description":"Load performance regression"}).ok)
	for pass_index in 2:
		started = Time.get_ticks_usec()
		assert(world.prepare_save_load(save).ok)
		var prepared := Time.get_ticks_usec()
		report = world.start_save_load({"logic_thread":false})
		assert(report.ok,str(report))
		assert(report.terrain.reused_gameplay_map)
		var restored := Time.get_ticks_usec()
		var step: Dictionary = world.save_load_step(100)
		while step.get("state","") == "catching_up": step = world.save_load_step(100)
		assert(step.state == "ready",str(step))
		assert(world.get_frame() == frame)
		assert(world.get_state_hash() == hash_before)
		print("LOAD PERF save ",pass_index," ",JSON.stringify({"wall_ms":(Time.get_ticks_usec()-started)/1000.0,"stages":report.timings_ms,"terrain":report.terrain.timings_ms,"snapshot_ms":step.get("snapshot_ms",0),"prepare_ms":(prepared-started)/1000.0,"start_ms":(restored-prepared)/1000.0,"handover_ms":(Time.get_ticks_usec()-restored)/1000.0}))
	world.free()
	print("MATCH LOAD PERFORMANCE PASS")
	quit()

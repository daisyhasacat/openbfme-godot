## Rewind a running match repeatedly without replacing its map or scene.
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
	var report: Dictionary = world.start_new_game({"logic_thread":OS.get_environment("QUICK_LOAD_THREADED") == "1","record":folder.path_join("in-place.BfME2Replay"),"test_hooks":true})
	assert(report.ok,str(report))
	world.set_auto_advance(false)
	for i in 5: world.advance(0.2)
	var frame: int = world.get_frame()
	var saved_hash: int = world.get_state_hash()
	var save := folder.path_join("in-place.obfmesave")
	assert(world.save_game(save,{"description":"Same-session rewind"}).ok)
	var children: Array = world.get_children()
	var original_ids: Array = world.get_object_ids()
	var future_hash := 0
	for pass_index in 3:
		for i in 10: world.advance(0.2)
		assert(world.get_frame() == frame + 10)
		if pass_index == 0: future_hash = world.get_state_hash()
		else: assert(world.get_state_hash() == future_hash)
		var added := 0
		if OS.get_environment("QUICK_LOAD_THREADED") != "1":
			added = world.create_object("GondorFighter", 1, 500.0, 500.0, 0.0)
			assert(world.get_object(added).ok)
			for id in original_ids:
				var object: Dictionary = world.get_object(id)
				if "Porter" in str(object.get("template", "")):
					assert(world.debug_damage_object(id, 1000000.0, 0).ok)
					break
		var started := Time.get_ticks_usec()
		var loaded: Dictionary = world.quick_load_game(save)
		assert(loaded.supported and loaded.ok,str(loaded))
		assert(world.get_frame() == frame)
		assert(world.get_state_hash() == saved_hash)
		assert(world.get_children() == children)
		assert(world.get_object_ids() == original_ids)
		print("IN PLACE LOAD ",JSON.stringify({"wall_ms":(Time.get_ticks_usec()-started)/1000.0,"load":loaded}))
		assert(world.save_game(folder.path_join("continued.obfmesave"),{}).ok)
	var bad := folder.path_join("bad.obfmesave")
	var output := FileAccess.open(bad, FileAccess.WRITE)
	assert(output != null)
	output.store_string("invalid save")
	output.close()
	var refused: Dictionary = world.quick_load_game(bad)
	assert(refused.supported and not refused.ok)
	assert(world.get_frame() == frame)
	assert(world.get_state_hash() == saved_hash)
	# A normal reload destroys the session identity, even when its map and save are identical.
	assert(world.prepare_save_load(folder.path_join("continued.obfmesave")).ok)
	assert(world.start_save_load({"logic_thread":false}).ok)
	assert(world.save_load_step(100).state == "ready")
	assert(world.get_state_hash() == saved_hash)
	assert(not world.quick_load_game(save).supported)
	world.free()
	print("SAME SESSION QUICK LOAD PASS")
	quit()

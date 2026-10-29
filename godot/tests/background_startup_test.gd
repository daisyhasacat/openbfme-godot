extends SceneTree

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	create_timer(90).timeout.connect(func(): quit(1))
	var fs = ClassDB.instantiate("RetailFileSystem")
	var frames := [0]
	var heartbeat := func(): frames[0] += 1
	process_frame.connect(heartbeat)
	var mounted: Dictionary = await root.get_node("Release").run_startup_task(fs.mount_retail)
	assert(mounted.ok, str(mounted))
	# tolerance: a background load must yield frames; the precise count depends on disk/CPU speed.
	assert(frames[0] > 0)
	var missing = ClassDB.instantiate("RetailFileSystem")
	var invalid_world = ClassDB.instantiate("GameWorld")
	assert(not invalid_world.begin_setup(missing).ok)
	invalid_world.free()
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.begin_setup(fs).ok)
	assert(not world.begin_setup(fs).ok) # no overlapping load into the same owner
	assert(not world.setup(missing).ok) # a mismatched finish cannot consume the task
	var waiting := false
	while not world.is_setup_ready():
		waiting = true
		await process_frame
	assert(waiting)
	var asynchronous: Dictionary = world.setup(fs)
	assert(asynchronous.ok, str(asynchronous))
	assert(not world.begin_setup(fs).ok) # loaded worlds keep the existing synchronous re-setup path
	var sync_world = ClassDB.instantiate("GameWorld")
	root.add_child(sync_world)
	var synchronous: Dictionary = sync_world.setup(fs)
	assert(synchronous.ok, str(synchronous))
	for key in ["templates", "factions", "warnings", "mod_data_errors", "errors"]:
		assert(asynchronous[key] == synchronous[key], key)
	assert(world.get_play_intro() == sync_world.get_play_intro())
	var audio = ClassDB.instantiate("GameAudio")
	root.add_child(audio)
	assert(not audio.begin_boot(missing, false).ok)
	assert(audio.begin_boot(fs, false).ok)
	assert(not audio.begin_boot(fs, false).ok)
	assert(not audio.boot(missing, {}).ok)
	while not audio.is_boot_ready(): await process_frame
	var background_audio: Dictionary = audio.boot(fs, {"seed":1})
	assert(background_audio.ok, str(background_audio))
	var names: PackedStringArray = audio.get_event_names(-1)
	audio.free()
	var sync_audio = ClassDB.instantiate("GameAudio")
	root.add_child(sync_audio)
	var immediate_audio: Dictionary = sync_audio.boot(fs, {"seed":1})
	assert(immediate_audio.ok, str(immediate_audio))
	assert(background_audio.events == immediate_audio.events)
	assert(names == sync_audio.get_event_names(-1))
	sync_audio.free()
	var abandoned_audio = ClassDB.instantiate("GameAudio")
	assert(abandoned_audio.begin_boot(fs, false).ok)
	abandoned_audio.free()
	var slots := []
	for i in 8: slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	for i in 2: slots[i] = {"state":6,"name":"Background test %d" % i,"accepted":true,"color":i,"start_pos":i,"player_template":4,"team":i}
	var request := {"mode":"skirmish","map_crc":0,"map_size":0,"map":"maps/map wor buckland/map wor buckland.map","seed":42,"starting_cash":2000,"slots":slots}
	assert(world.prepare_new_game(request).ok)
	assert(sync_world.prepare_new_game(request).ok)
	assert(world.start_new_game({"logic_thread":false}).ok)
	assert(sync_world.start_new_game({"logic_thread":false}).ok)
	assert(world.get_state_hash() == sync_world.get_state_hash())
	for i in 5:
		world.advance(0.2)
		sync_world.advance(0.2)
		assert(world.get_state_hash() == sync_world.get_state_hash())
	world.free()
	sync_world.free()
	# Destruction while loading joins before the archive owner can be released.
	var abandoned = ClassDB.instantiate("GameWorld")
	assert(abandoned.begin_setup(fs).ok)
	abandoned.free()
	process_frame.disconnect(heartbeat)
	print("BACKGROUND STARTUP PASS: frames continue, setup reports and game hashes match, shutdown joins")
	quit()

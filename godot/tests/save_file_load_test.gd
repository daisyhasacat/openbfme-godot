## Load an existing save without changing it, advance two frames, and optionally save/reload a temporary copy.
## OPENBFME_SAVE_PATH selects the input. OPENBFME_SAVE_ROUNDTRIP selects an optional output file.
## Uses the configured retail install folders. Missing input environment variable exits 77.
extends SceneTree

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var path := OS.get_environment("OPENBFME_SAVE_PATH")
	if path.is_empty():
		print("SKIP: set OPENBFME_SAVE_PATH to an existing save")
		quit(77)
		return
	var cfg: Dictionary = ClassDB.instantiate("InstallSetup").configured()
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail_paths(cfg.rotwk, cfg.bfme2)
	if not _ok(mount, "mount"):
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not _ok(world.setup(fs), "setup"):
		return
	if not _load_save(world, path):
		return
	var before: int = world.get_frame()
	world.advance(0.2)
	world.advance(0.2)
	assert(world.get_frame() == before + 2)
	var output := OS.get_environment("OPENBFME_SAVE_ROUNDTRIP")
	if not output.is_empty():
		assert(output != path)
		if not _ok(world.save_game(output, {"description": "Save load regression"}), "save temporary copy"):
			return
		var expected: int = world.get_frame()
		var expected_hash: int = world.get_state_hash()
		if not _load_save(world, output):
			return
		assert(world.get_frame() == expected)
		assert(world.get_state_hash() == expected_hash)
	print("SAVE FILE LOAD PASS frame ", world.get_frame())
	quit(0)

func _ok(result: Dictionary, step: String) -> bool:
	if result.get("ok", false):
		return true
	printerr("SAVE FILE LOAD FAIL ", step, ": ", result.get("errors", result.get("error", result)))
	quit(1)
	return false

func _load_save(world: Node, path: String) -> bool:
	if not _ok(world.prepare_save_load(path), "prepare"):
		return false
	if not _ok(world.start_save_load({"logic_thread": false}), "start"):
		return false
	# Both direct snapshots and older command-log saves must reach an explicit result.
	var result: Dictionary = world.save_load_step(10)
	while result.get("state", "") == "catching_up":
		result = world.save_load_step(50)
	print("SAVE FILE LOAD ", result)
	if result.get("state", "") != "ready":
		printerr("SAVE FILE LOAD FAIL: ", result)
		quit(1)
		return false
	return true

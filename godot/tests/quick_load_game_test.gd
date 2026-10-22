## Restore an isolated copy of a real save from menus and gameplay without a loading picture.
## OPENBFME_SAVE_PATH supplies a save; the original is never modified.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var source := OS.get_environment("OPENBFME_SAVE_PATH")
	if source.is_empty():
		print("SKIP: set OPENBFME_SAVE_PATH")
		quit(77)
		return
	var folder := OS.get_environment("TMPDIR").path_join("openbfme-quick-load-%d" % OS.get_process_id())
	assert(DirAccess.make_dir_recursive_absolute(folder) == OK)
	var path := folder.path_join("Quick Save.obfmesave")
	assert(DirAccess.copy_absolute(source, path) == OK)
	var game = load("res://scenes/game.tscn").instantiate()
	root.add_child(game)
	while game._state == game.State.BOOT: await process_frame
	assert(game._state == game.State.MENU)
	game._save_dir_arg = folder
	# Progress callbacks render synchronously; quick load must not invoke them either.
	game._load_log.clear()
	var first: Dictionary = await game._save_flow.quick_load()
	assert(first.ok, str(first))
	assert(game._state == game.State.PLAYING)
	game._world.set_auto_advance(false)
	game._world.advance(0.2)
	var frame: int = game._world.get_frame()
	var saved: Dictionary = game.save_game()
	assert(saved.ok, str(saved))
	var expected_hash: int = game._world.get_state_hash()
	var second: Dictionary = await game._save_flow.quick_load()
	assert(second.ok, str(second))
	assert(game._state == game.State.PLAYING)
	game._world.set_auto_advance(false)
	assert(game._world.get_frame() == frame)
	assert(game._world.get_state_hash() == expected_hash)
	assert(game._load_log == [])
	game.queue_free()
	await process_frame
	for file in DirAccess.get_files_at(folder): DirAccess.remove_absolute(folder.path_join(file))
	DirAccess.remove_absolute(folder)
	print("QUICK LOAD GAME PASS menus, gameplay, frame and hash restored; no load progress screen")
	quit(0)

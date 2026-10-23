## BFME2 selection mounts verified BFME2 data without the RotWK overlay.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	create_timer(90).timeout.connect(func(): push_error("BFME2 gameplay test timed out"); quit(1))
	var release = root.get_node("Release")
	assert(release.selected_game() == "bfme2-106")
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = await release.mount_retail(fs)
	assert(mounted.ok, str(mounted))
	assert(not fs.is_bfme1())
	assert(not fs.is_shell_source())
	assert(mounted.stops == PackedStringArray(["[S-5150] BFME2 1.06 gameplay loads BFME2-only data on the shared simulation; BFME2 versus RotWK runtime differences are not fully verified"]))
	assert(not mounted.mounted.is_empty())
	for archive in mounted.mounted:
		assert(archive.install == "bfme2", str(archive))
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	assert(setup.ok, str(setup))
	assert(setup.factions == 6)
	var loaded: Dictionary = world.load_map("map mp fords of isen ii", {"slots":[{"player":"Player_1","faction":"FactionMen","human":true}]})
	assert(loaded.ok, str(loaded))
	var first_frame: int = world.get_frame()
	world.set_time_scale(1.0)
	world.set_paused(false)
	for i in 10: world.advance(0.2)
	assert(world.get_frame() == first_frame + 10, str(world.get_frame(), " from ", first_frame))
	print("BFME2 GAMEPLAY PASS: BFME2-only archives, six factions and live map simulation")
	world.free()
	var game = load("res://scenes/game.tscn").instantiate()
	root.add_child(game)
	while game._state == game.State.BOOT: await process_frame
	assert(game._state == game.State.MENU)
	assert(game._menu_style == "bfme2")
	assert(game._shell.get_shell_report().errors.is_empty(), str(game._shell.get_shell_report()))
	print("BFME2 MENU PASS")
	game.queue_free()
	await process_frame
	quit()

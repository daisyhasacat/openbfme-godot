## Installed BFME2 Basic tutorial: ownership and rendered house-color variants follow transfer.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	create_timer(180).timeout.connect(func(): printerr("TUTORIAL TEAM COLOR TIMEOUT"); quit(1))
	var game = load("res://scripts/game.gd").new()
	game._intro = "off"
	root.add_child(game)
	while game._campaign_flow == null or game._state not in [game.State.MENU, game.State.FAILED]: await process_frame
	assert(game._state == game.State.MENU, game._failed)
	await game._wait_main_ready()
	var world: Node = game._world
	world.set_logic_thread(false)
	game._campaign_flow.hooks = true
	game._campaign_flow.start_tutorial("Basic")
	while game._state not in [game.State.PLAYING, game.State.FAILED]: await process_frame
	assert(game._state == game.State.PLAYING, game._failed)
	world.set_auto_advance(false)
	game._campaign_flow._cine.set_process(false)
	var horde: Dictionary = world.debug_script_unit("Fighters 01")
	assert(horde.ok)
	var members: Array = world.get_object(horde.id).members
	assert(members.size() == 15)
	for target in ["<Local Player>", "PlyrCivilian", "<Local Player>"]:
		assert(world.debug_script_action("NAMED_TRANSFER_OWNERSHIP_PLAYER", [
			{"type":14,"string":"Fighters 01"},{"type":11,"string":target}]).ok)
		world.advance(0.2)
		for i in 5: await process_frame
		var owner := "PlyrMen" if target == "<Local Player>" else "PlyrCivilian"
		# Installed Basic map: PlyrMen RGB 0,128,192; PlyrCivilian white.
		var color := -16744256 if owner == "PlyrMen" else -1
		assert(world.get_object(horde.id).owner == owner)
		for id in members:
			var member: Dictionary = world.get_object(id)
			assert(member.owner == owner)
			var models: Array = member.get("shown_models", [])
			assert(not models.is_empty())
			for model in models:
				assert(str(model).contains("#1&%d&0&0" % color), str(model))
	print("TUTORIAL TEAM COLOR PASS: all 15 soldiers transfer and recolor in both directions")
	game.free()
	quit()

## Optional integration test against an external AOTR installation; no retail bytes in Git.
extends SceneTree
func _initialize() -> void:
	create_timer(180).timeout.connect(func(): push_error("AOTR startup timed out"); quit(1))
	call_deferred("run")
func run() -> void:
	if OS.get_environment("AOTR_INSTALL").is_empty() or OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("AOTR STARTUP SKIP: AOTR_INSTALL, ROTWK_INSTALL and BFME2_INSTALL required")
		quit(77)
		return
	root.size = Vector2i(1280, 720)
	var folder: String = root.get_node("Release").aotr_folder(OS.get_environment("AOTR_INSTALL"))
	assert(not folder.is_empty())
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_aotr_path(folder)
	assert(mount.ok, str(mount.get("errors", [])))
	assert(mount.mods.size() == 2)
	assert(mount.mods[0].path.ends_with("rotwk/#aotr_patch202.big"))
	assert(mount.mods[1].path == folder)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	assert(setup.ok, str(setup.get("errors", [])))
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	var boot: Dictionary = shell.boot_shell(fs, world, {"skirmish_ini":"user://aotr-startup-test.ini"})
	assert(boot.ok, str(boot))
	assert(shell.shell_push("Skirmish.apt"))
	for i in 80:
		shell.tick(1.0 / 30.0)
		shell.render(true)
	assert(shell.lobby_state().found)
	var lobby: Dictionary = shell.lobby_state()
	assert(lobby.rules_profile == "Age of the Ring")
	var cash_found := false
	for rule in lobby.rule_definitions:
		if int(rule.id) == 4:
			cash_found = true
			assert(int(rule.default) == 1500)
			var values: Array = []
			for choice in rule.choices: values.append(int(choice.value))
			assert(values == [500, 1000, 1500, 2000, 2500, 10000000])
	assert(cash_found)
	assert(shell.lobby_apply({"browser_rules":[{"id":4,"value":1500}]}).ok)
	assert(int(shell.lobby_state().rules[4]) == 1500)
	assert(shell.get_shell_report().errors.is_empty(), str(shell.get_shell_report().errors))
	# tolerance: a successfully loaded menu must submit visible canvas content; prevents a backdrop-only false pass.
	assert(shell.get_stats().last_ops > 0)
	assert(not shell.get_layering_report().screens.is_empty())
	if DisplayServer.get_name() != "headless" and not OS.get_environment("AOTR_MENU_SHOT").is_empty():
		await process_frame
		await RenderingServer.frame_post_draw
		assert(root.get_texture().get_image().save_png(OS.get_environment("AOTR_MENU_SHOT")) == OK)
	var map_name := OS.get_environment("AOTR_TEST_MAP")
	if map_name.is_empty(): map_name = "map mp itakh khand"
	var map_key := map_name if map_name.begins_with("maps/") else "maps/%s/%s.map" % [map_name, map_name]
	var slots: Array = []
	for i in 8:
		slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	var rohan: Array = lobby.factions.filter(func(faction): return faction.template == "FactionRohan")
	assert(rohan.size() == 1)
	slots[0] = {"state":6,"name":"AOTR test","accepted":true,"color":-1,"start_pos":0,"player_template":rohan[0].value,"team":0}
	var prepared: Dictionary = world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,
		"map":map_key,"map_crc":0,"map_size":0,"map_mask":0,"seed":1,"starting_cash":1500,"slots":slots})
	assert(prepared.ok, str(prepared.get("errors", [])))
	var map: Dictionary = world.start_new_game({"logic_thread":false})
	assert(map.ok, str(map.get("errors", [])))
	assert(map.start.starting_objects.any(func(object): return object.template == "CommandPointSpawn"))
	var before: int = world.get_frame()
	world.advance(0.2)
	assert(world.get_frame() != before)
	shell.free()
	world.free()
	print("AOTR STARTUP PASS: world, skirmish menu, map and simulation")
	quit()

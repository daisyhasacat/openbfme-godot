extends SceneTree
## iOS clears builders only after a valid construction order.
func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("IOS BUILDER DESELECT SKIP: retail installations required")
		quit(77)
		return
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.load_map("map mp fall back 4p", {"slots": [
		{"player":"Player_1", "faction":"FactionMen", "human":true},
		{"player":"Player_2", "faction":"FactionMordor", "human":false}]}).ok)
	var owner: Dictionary = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then p.money=100000; return id end end")
	assert(owner.ok, str(owner))
	world.update_lua()
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs, world, camera, "Player_1", {"camera_start":Vector3(1800,0,-1800),"prewarm_buildings":false,"deselect_builders_after_build":true}).ok)
	var warmed_models: int = hud.get_placement_ghost_state().model_loads
	assert(warmed_models == 0)
	var builder: int = world.create_object("MenPorter", int(owner.output), 1800, 1800, 0)
	assert(world.get_object(builder).ok)
	world.advance(0.2)
	hud.select_controller_group(PackedInt64Array([builder]))
	for i in 4: await process_frame
	var command := {}
	for entry in hud.get_command_buttons():
		if entry.command == "DOZER_CONSTRUCT": command = entry; break
	assert(not command.is_empty())
	assert(hud.activate_command(command.slot, command.in_palantir, false))
	var valid := false
	for y in [250, 300, 350, 400]:
		for x in [400, 500, 600, 700, 800, 900]:
			assert(hud.touch_build("position", Vector2(x,y)))
			if hud.get_placement().legal == 0:
				valid = true
				break
		if valid: break
	assert(valid, "fixture needs a legal building site")
	assert(hud.get_selection() == [builder])
	assert(hud.touch_build("lock", Vector2.ZERO))
	assert(hud.touch_build("confirm", Vector2.ZERO))
	for i in 4: await process_frame
	assert("MSG_DOZER_CONSTRUCT" in str(hud.get_message_log()), str(hud.get_message_log()))
	assert(hud.get_selection() == [])
	print("IOS BUILDER DESELECT PASS")
	quit()

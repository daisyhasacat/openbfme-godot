extends SceneTree
# Wall span previews: every other legal wall segment is highlighted (OpenBFME placement aid). Needs ROTWK_INSTALL / BFME2_INSTALL.
# Options: -- --men (Men's small hub instead of the Elves'), --shot=<png>
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	root.size = Vector2i(1280,720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.load_map("map mp fall back 4p", {"slots":[{"player":"Player_1","faction":"FactionElves","human":true}]}).ok)
	var player = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then p.money=100000; return id end end")
	assert(player.ok)
	var hub_template = "MenWallHubSmall" if "--men" in OS.get_cmdline_user_args() else "ElvenCastleWallHub"
	var hub = world.create_object(hub_template,int(player.output),1800,1800,0)
	# tolerance: Object IDs are allocated dynamically; zero is the invalid ID.
	assert(hub > 0)
	var camera = Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs,world,camera,"Player_1",{"camera_start":Vector3(2000,0,-1800),"alternate_mouse":true}).ok)
	world.set_shroud_drawn(false)
	hud.set_controller_layout(true)
	hud.select_controller_group(PackedInt64Array([hub]))
	world.advance(0.2)
	for i in 4: await process_frame
	var span := {}
	for command in hud.get_command_buttons():
		if command.get("template", "") == hub_template: span = command
	assert(not span.is_empty(),str(hud.get_command_buttons()))
	assert(hud.activate_command(span.slot,span.in_palantir,false))
	hud.inject_mouse_move(hud.world_to_pixel(Vector2(2400,1800)))
	for i in 6: await process_frame
	var preview: Dictionary = hud.get_placement_ghost_state()
	var segment := 0
	var highlighted := 0
	var plain := 0
	for icon in preview.icons:
		if "Hub" in str(icon.template) or icon.code != 0:
			continue
		var gold: bool = icon.tint.a > 0 and icon.tint.r > 0 and icon.tint.b == 0
		assert(gold == (segment % 2 == 1), "segment %d tint %s: %s" % [segment, str(icon.tint), str(preview.icons)])
		if gold: highlighted += 1
		else: plain += 1
		segment += 1
	# tolerance: The preview must contain both aligned and unaligned spans; span counts depend on tessellation.
	assert(highlighted >= 1 and plain >= 1, str(preview.icons))
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shot="):
			root.get_viewport().get_texture().get_image().save_png(arg.trim_prefix("--shot="))
	print("WALL ALTERNATE HIGHLIGHT PASS: %d plain, %d highlighted" % [plain, highlighted])
	hud.controller_deselect()
	hud.queue_free()
	world.queue_free()
	camera.queue_free()
	await process_frame
	quit()

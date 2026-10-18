extends SceneTree
# Wall spans: while a span is dragged from a hub, the hub's existing spans at exactly 90 degrees or exactly opposite are outlined (OpenBFME placement aid).
# Needs ROTWK_INSTALL / BFME2_INSTALL. Option: -- --shot=<png>
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	root.size = Vector2i(1280,720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.load_map("map mp fall back 4p", {"slots":[{"player":"Player_1","faction":"FactionMen","human":true}]}).ok)
	var player = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then p.money=100000; return id end end")
	assert(player.ok)
	var hub_template := "MenWallHubSmall"
	var hub = world.create_object(hub_template,int(player.output),1800,1800,0)
	# tolerance: Object IDs are allocated dynamically; zero is the invalid ID.
	assert(hub > 0)
	# east (opposite the drag below), north (square to it) and north-east (neither)
	for end in [Vector2(2200,1800), Vector2(1800,2200), Vector2(2100,2100)]:
		assert(world.build2_wall_span(hub,hub_template,1800,1800,end.x,end.y,0), str(end))
		for i in 150: world.advance(0.2)
	var camera = Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs,world,camera,"Player_1",{"camera_start":Vector3(1800,0,-1800),"alternate_mouse":true}).ok)
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
	world.set_logic_thread(true)
	hud.set_placement_snap(true, 50) # the drag lands exactly on the map's axes
	hud.inject_mouse_move(hud.world_to_pixel(Vector2(1500,1800)))
	for i in 30:
		world.advance(1.0/60.0)
		await process_frame
	var at: Dictionary = hud.get_placement()
	# tolerance: The snap must remain on the horizontal axis and extend left of its anchor.
	assert(is_equal_approx(at.y, 1800.0) and at.x < 1800.0, str(at))
	var state: Dictionary = hud.get_placement_ghost_state()
	assert(state.wall_aligned_spans == 2, str(state))
	# tolerance: Both aligned candidate spans must be reported; individual spans can contain multiple segments.
	assert(state.wall_aligned_segments >= 2, str(state))
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shot="):
			root.get_viewport().get_texture().get_image().save_png(arg.trim_prefix("--shot="))
	# Same direction (0 degrees), using the live game's threaded presentation path.
	hud.inject_mouse_move(hud.world_to_pixel(Vector2(2100,1800)))
	for i in 30:
		world.advance(1.0/60.0)
		await process_frame
	state = hud.get_placement_ghost_state()
	assert(state.wall_aligned_spans == 2,str(state))
	# a drag square to nothing: south-west by north-west (the diagonal span's 135 degrees is neither)
	hud.inject_mouse_move(hud.world_to_pixel(Vector2(1550,1700)))
	for i in 30:
		world.advance(1.0/60.0)
		await process_frame
	state = hud.get_placement_ghost_state()
	assert(state.wall_aligned_spans == 0, str(state))
	print("WALL SPAN ALIGNMENT PASS")
	hud.controller_deselect()
	hud.queue_free()
	world.queue_free()
	camera.queue_free()
	await process_frame
	quit()

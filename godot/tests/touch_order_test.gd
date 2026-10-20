extends SceneTree
func _initialize() -> void: call_deferred("_run")
func touch(hud: Node, index: int, down: bool, pos: Vector2) -> void:
	var event := InputEventScreenTouch.new()
	event.index = index
	event.pressed = down
	event.position = pos
	Input.parse_input_event(event)
func selection_pixel(hud: Node, id: int, position: Vector2) -> Vector2:
	var center: Vector2 = hud.world_to_pixel(position)
	for y in range(-80, 81, 4):
		for x in range(-80, 81, 4):
			var pixel := center + Vector2(x, y)
			if hud.pick_probe(pixel, id).picked == id: return pixel
	assert(false, "No selection pixel for object %s" % id)
	return center
func _run() -> void:
	root.size = Vector2i(1280,720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.load_map("map mp fall back 4p", {"slots":[{"player":"Player_1","faction":"FactionMen","human":true}]}).ok)
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs,world,camera,"Player_1",{"camera_start":Vector3(1800,0,-1800)}).ok)
	for i in 3: await process_frame
	var owner = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then return id end end")
	var unit: int = world.create_object("GondorFighter", int(owner.output), 1800, 1800, 0)
	assert(unit > 0)
	world.advance(0.2)
	hud.select_controller_group(PackedInt64Array([unit]))
	for i in 3: await process_frame
	var before: Dictionary = world.get_object(unit)
	assert(not hud.get_selection().is_empty())
	# Aim at known nearby terrain instead of an arbitrary screen point, which
	# can project outside the playable map as camera projection changes.
	var destination := Vector2(2000, 1800)
	var pixel: Vector2 = hud.world_to_pixel(destination)
	for down in [true,false]:
		var event := InputEventScreenTouch.new()
		event.index = 0
		event.pressed = down
		event.position = pixel
		Input.parse_input_event(event)
		world.advance(0.2)
		await process_frame
	for i in 30:
		world.advance(0.2)
		await process_frame
	var after: Dictionary = world.get_object(unit)
	assert(hud.get_state().keys.touch_orders == 1, str(hud.get_state()))
	assert("MSG_DO_MOVETO" in str(hud.get_message_log()), str(hud.get_message_log()))
	assert(Vector2(before.x,before.y).distance_to(Vector2(after.x,after.y)) > 10, str(after))
	assert(not hud.get_selection().is_empty())
	# Object taps replace an existing selection instead of issuing move/attack
	# orders, including when the new selection is a building.
	for template in ["GondorFighter", "GondorBarracks"]:
		var position := Vector2(1850, 1650) if template == "GondorFighter" else Vector2(1600, 1800)
		var target: int = world.create_object(template, int(owner.output), position.x, position.y, 0)
		assert(world.get_object(target).ok)
		world.advance(0.2)
		for i in 3: await process_frame
		pixel = selection_pixel(hud, target, position)
		for down in [true, false]:
			touch(hud, 0, down, pixel)
			await process_frame
		for i in 3: await process_frame
		assert(hud.get_selection() == [target], str(hud.get_selection()))
		assert(hud.get_state().keys.touch_orders == 1)
	print("TOUCH CONTEXT ORDER ROUTING PASS")
	quit()

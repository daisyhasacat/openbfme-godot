## GPU regression: fortress builder and hero radial cameos must retain the RGB background
## stored beneath transparent texels. Run without --headless with the retail installs mounted.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if DisplayServer.get_name() == "headless":
		print("RADIAL BACKGROUND SKIP: requires a GPU")
		quit(77)
		return
	create_timer(45.0).timeout.connect(func(): quit(1)) # A failed assertion must not leave the test running.
	root.size = Vector2i(1024, 768)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var report: Dictionary = world.load_map("map mp fall back 4p", {"slots": [{"player": "Player_1", "faction": "FactionMen", "human": true}]})
	assert(report.ok)
	world.set_logic_thread(false)
	var owner := -1
	for i in report.players.size():
		if String(report.players[i]).contains("'Player_1'"): owner = i
	assert(owner != -1)
	var fortress: int = world.create_object("MenFortressCitadel", owner, 1800, 1800, 0)
	assert(fortress != -1)
	world.advance(0.2)
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs, world, camera, "Player_1", {"camera_start": Vector3(1800, 0, -1800)}).ok)
	hud.select_object(fortress)
	for i in 90:
		world.advance(0.033)
		await process_frame
	world.hide()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	camera.environment = environment
	environment.background_color = Color.RED
	await process_frame
	await RenderingServer.frame_post_draw
	var red := root.get_texture().get_image()
	environment.background_color = Color.BLUE
	await process_frame
	await RenderingServer.frame_post_draw
	var blue := root.get_texture().get_image()
	var checked: Array[String] = []
	for button in hud.get_hud6_state().radial.buttons:
		if button.name not in ["Command_ConstructMenPorter", "Command_SelectRevivablesMenFortress"]: continue
		var mapped: Dictionary = hud.get_mapped_image(button.image)
		assert(mapped.found)
		var texture: Image = mapped.texture.get_image()
		if texture.is_compressed(): assert(texture.decompress() == OK)
		var region: Rect2 = mapped.region
		var rect: Rect2 = button.rect
		var samples := 0
		for y in range(4, 44):
			for x in range(4, 44):
				var uv := Vector2(x + 0.5, y + 0.5) / 48.0
				var texel := texture.get_pixelv(Vector2i(region.position + uv * region.size))
				if (uv - Vector2(0.5, 0.5)).length() > 0.35: continue
				if button.name == "Command_ConstructMenPorter" and texel.a >= 0.99: continue
				var pixel := Vector2i(rect.position) + Vector2i(x, y)
				assert(red.get_pixelv(pixel) == blue.get_pixelv(pixel), "Radial background leaks through " + button.name)
				samples += 1
		assert(samples > 0, "Fixture needs sampled interior texels: " + button.name) # tolerance: checks that the retail artwork exercises the alpha regression
		checked.append(button.name)
	assert(checked == ["Command_ConstructMenPorter", "Command_SelectRevivablesMenFortress"], str(checked))
	print("RADIAL BACKGROUND PASS: builder and hero buttons retain their backgrounds")
	hud.free()
	world.free()
	quit()

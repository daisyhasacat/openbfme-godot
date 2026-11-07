## Live skirmish regression for TheTaintManager on the terrain (S-6110 / S-6111). Requires installed data:
##   godot --path godot --script res://tests/taint_terrain_test.gd -- --auto --faction=FactionMordor --advance=600
## Mordor buys Tainted Land through Lua, casts it beside its fortress, and the terrain's material must take the taint mask with its two textures; the mask's
## texel under the cast is tainted (alpha below neutral). With a graphical renderer and TAINT_SCREENSHOT=<png> the view of the cast is saved.
extends SceneTree

func _initialize() -> void: call_deferred("_run")

func _lua(game, code: String) -> String:
	var r: Dictionary = game._world.execute_lua(code)
	assert(r.ok, str(r))
	game._world.update_lua()
	return str(r.output).strip_edges()

func _run() -> void:
	var game = load("res://scenes/game.tscn").instantiate()
	root.add_child(game)
	for i in 10000:
		await process_frame
		# tolerance: wait for a logic update after match setup before inspecting the live objects.
		if game._state == game.State.PLAYING and game._hud != null and game._world.get_frame() >= 2: break
	assert(game._state == game.State.PLAYING)
	var center := Vector2.ZERO
	var count := 0
	for object in game._world.get_player_objects(game._local_name):
		if str(object.get("template", "")).find("Fortress") >= 0:
			center += Vector2(object.x, object.y)
			count += 1
	assert(not count == 0)
	center /= count
	var spot := center + Vector2(250, 0)
	_lua(game, "bfme.players.localPlayer.powerPoints = 100000")
	for i in 20: await process_frame
	for round in 6:
		_lua(game, "local p=bfme.players.localPlayer; for _,s in ipairs(p:getSciences()) do if not s.owned and s.canPurchase then p:purchaseScience(s.science) end end")
		for i in 20: await process_frame
	assert(_lua(game, "for _,s in ipairs(bfme.players.localPlayer:getSciences()) do if s.science=='SCIENCE_Taint' then print(s.owned) end end") == "true")
	_lua(game, "bfme.players.localPlayer:castPower('SpellBookTaint', {x=%f, y=%f})" % [spot.x, spot.y])
	var mat: ShaderMaterial = null
	for i in 600:
		await process_frame
		for node in game._world.find_children("*", "MeshInstance3D", true, false):
			var m = node.get_active_material(0)
			if m is ShaderMaterial and m.get_shader_parameter("taint_enabled") == true:
				mat = m
				break
		if mat != null: break
	assert(mat != null, "no terrain material took the taint mask")
	var tex: Texture2D = mat.get_shader_parameter("taint_tex")
	assert(tex != null)
	assert(mat.get_shader_parameter("taint_low_tex") != null)
	assert(mat.get_shader_parameter("taint_high_tex") != null)
	var origin: Vector2 = mat.get_shader_parameter("taint_origin")
	var extent: Vector2 = mat.get_shader_parameter("taint_extent")
	var img := tex.get_image()
	var uv := (spot - origin) / extent
	var texel := img.get_pixel(int(uv.x * img.get_width()), int(uv.y * img.get_height()))
	print("TAINT mask at the cast: ", texel, " texture ", img.get_width(), "x", img.get_height())
	# W3DTaintInterpolate fades the displayed mask; enabling the material can precede the first byte step.
	var deadline := Time.get_ticks_msec() + 5000
	while texel.a8 != 0 and Time.get_ticks_msec() < deadline:
		await process_frame
		img = tex.get_image()
		texel = img.get_pixel(int(uv.x * img.get_width()), int(uv.y * img.get_height()))
	assert(texel.a8 == 0, "the cast's displayed cell did not finish fading to taint")
	# BFME2 shader terrain mode > 0 leaves RGB white; legacy tint would brown the ground twice.
	assert(texel.r8 == 255)
	assert(texel.g8 == 255)
	assert(texel.b8 == 255)
	var far := img.get_pixel(1, 1)
	assert(far.a8 == 0x80, "a far cell is not neutral")
	if DisplayServer.get_name() != "headless":
		game._hud.camera_look_at(spot)
		for i in 60:
			await process_frame
			game._hud.camera_look_at(spot)
		await RenderingServer.frame_post_draw
		var shot := OS.get_environment("TAINT_SCREENSHOT")
		if not shot.is_empty():
			root.get_texture().get_image().save_png(shot)
	print("TAINT TERRAIN PASS")
	quit()

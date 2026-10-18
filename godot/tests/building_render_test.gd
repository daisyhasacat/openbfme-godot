## Render real building materials through Forward+; headless tests do not compile them.
## Run without --headless, with ROTWK_INSTALL and BFME2_INSTALL set.
## Regression: separate flat light/fog/team varyings exceeded Godot's 31-location
## budget, so buildings fell back to untextured, incorrectly positioned geometry.
extends SceneTree

var _failures := 0

func _initialize() -> void:
	call_deferred("_run")

func _check(ok: bool, message: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", message)
	if not ok:
		_failures += 1

func _run() -> void:
	if DisplayServer.get_name() == "headless" or OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: building rendering needs a GPU/window and retail install paths")
		quit(77)
		return
	if RenderingServer.get_current_rendering_method() != "forward_plus":
		print("SKIP: building regression requires --rendering-method forward_plus")
		quit(77)
		return
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		_check(false, "retail mount succeeds")
		quit(1)
		return
	var world: Node3D = ClassDB.instantiate("GameWorld")
	var setup: Dictionary = world.setup(fs)
	if not setup.ok or not world.load_map("map good celduin", {"seed": 5}).ok:
		_check(false, "object world loads")
		world.free()
		quit(1)
		return
	for building in ["GondorBarracks", "MenFortressCitadel", "MordorFortressCitadel"]:
		var id: int = world.create_object(building, 0, 800.0, 800.0, 0.0)
		world.advance(0.2) # publish the creation event and resolve its draw models
		var info: Dictionary = world.get_object(id)
		_check(info.get("ok", false) and not info.get("models", []).is_empty(), "%s has a model" % building)
		if not info.get("ok", false) or info.get("models", []).is_empty():
			continue
		for team_color in [false, true]:
			await _render(fs, info.models[0], team_color)
	world.free()
	print("BUILDING RENDER %s" % ("PASS" if _failures == 0 else "FAIL"))
	quit(0 if _failures == 0 else 1)

func _render(fs: RefCounted, model_name: String, team_color: bool) -> void:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(256, 256)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_mode = Environment.BG_COLOR
	environment.environment.background_color = Color.BLACK
	viewport.add_child(environment)
	var inst: Node3D = ClassDB.instantiate("W3DInstancer")
	inst.set_house_colors_enabled(team_color)
	viewport.add_child(inst)
	inst.setup(fs)
	var model: int = inst.add_model(model_name)
	_check(model >= 0, "build %s (team colour %s)" % [model_name, team_color])
	if model >= 0:
		var instance: int = inst.add_instance(model, Transform3D.IDENTITY, "", 0.0, 1.0)
		if team_color:
			inst.set_instance_house_color(instance, Color.RED)
		inst.update_now()
		var model_report: Dictionary = inst.get_model_report(model)
		_check(model_report.fx_surfaces > 0, "%s includes FX surfaces" % model_name)
		var radius: float = inst.get_model_radius(model)
		var camera := Camera3D.new()
		camera.projection = Camera3D.PROJECTION_ORTHOGONAL
		camera.size = radius * 3.0
		camera.far = radius * 10.0
		viewport.add_child(camera)
		camera.position = Vector3(radius * 2.0, radius * 2.0, radius * 2.0)
		camera.look_at(Vector3(0, radius * 0.25, 0))
		camera.current = true
		await process_frame
		await process_frame
		await RenderingServer.frame_post_draw
		var image := viewport.get_texture().get_image()
		var colors := {}
		for y in range(16, 240):
			for x in range(16, 240):
				colors[image.get_pixel(x, y).to_rgba32()] = true
		# tolerance: rasterization differs by GPU; flat fallback materials lack the retail texture's colour variation.
		_check(colors.size() > 64, "%s renders textured geometry (%d colours, team %s)" % [model_name, colors.size(), team_color])
	viewport.free()

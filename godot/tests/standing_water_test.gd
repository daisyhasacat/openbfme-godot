## Authored standing-water materials, map depth inputs and GPU compilation.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	create_timer(90).timeout.connect(func(): push_error("Standing water test timed out"); quit(1))
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var map = builder.build_map(fs, "map mp harlindon", {"markers":false})
	assert(map != null)
	root.add_child(map)
	var report: Dictionary = builder.get_report()
	print("WATER MATERIALS ", report.water)
	assert(report.errors.is_empty(), str(report.errors))
	assert(report.water.materials.size() == 1)
	assert(report.water.materials[0].color == Vector4(0.15, 0.3, 0.35, 0.9))
	assert(report.water.materials[0].bump_uv_scale == Vector2(15, 15))
	assert(report.water.max_alpha_depth == 20.0)
	assert(report.water.deep_water_alpha == 1.0)
	var checked := 0
	var authored := 0
	for info in report.water.materials:
		if info.authored:
			authored += 1
	for mesh in map.get_node("StandingWater").get_children():
		var material: ShaderMaterial = mesh.mesh.surface_get_material(0)
		assert(material != null)
		assert(material.get_shader_parameter("terrain_height_tex") != null)
		assert(material.get_shader_parameter("material_diffuse") == report.water.materials[checked].color)
		assert(material.get_shader_parameter("bump_uv_scale") == report.water.materials[checked].bump_uv_scale)
		checked += 1
	assert(checked == report.water.areas)
	assert(authored == checked)
	assert(checked > 0) # tolerance: this retail map must exercise standing water.
	if DisplayServer.get_name() != "headless":
		root.size = Vector2i(1000, 700)
		var camera := Camera3D.new()
		root.add_child(camera)
		camera.position = Vector3(2500, 1500, -1500)
		camera.look_at(Vector3(1700, 0, -2500))
		camera.far = 15000
		camera.current = true
		for frame in 30:
			await process_frame
		await RenderingServer.frame_post_draw
		var image := root.get_texture().get_image()
		assert(image != null and not image.is_empty())
		var capture := OS.get_environment("OPENBFME_WATER_SCREENSHOT")
		if not capture.is_empty():
			assert(image.save_png(capture) == OK)
	map.queue_free()
	print("STANDING WATER PASS: ", checked, " materials")
	quit()

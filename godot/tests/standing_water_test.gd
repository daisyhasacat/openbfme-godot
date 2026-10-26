## Authored standing-water materials, map depth inputs and GPU compilation.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _check_pca_animation(source: ShaderMaterial) -> bool:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(128, 128)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var material: ShaderMaterial = source.duplicate(false)
	material.set_shader_parameter("blend_terrain_alpha", false)
	material.set_shader_parameter("material_diffuse", Vector4(0.15, 0.3, 0.35, 1.0))
	material.set_shader_parameter("output_gamma", 2.2)
	material.set_shader_parameter("pca_frame_rate", 0.0)
	material.set_shader_parameter("wave_geometry_enabled", false)
	var mesh := PlaneMesh.new()
	mesh.size = Vector2(800, 800)
	mesh.material = material
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	viewport.add_child(instance)
	var camera := Camera3D.new()
	viewport.add_child(camera)
	camera.position = Vector3(0, 800, 600)
	camera.look_at(Vector3.ZERO)
	camera.current = true
	for frame in 5:
		await process_frame
	await RenderingServer.frame_post_draw
	var frozen := viewport.get_texture().get_image().get_data()
	await create_timer(0.15).timeout
	await RenderingServer.frame_post_draw
	var still_frozen := viewport.get_texture().get_image().get_data()
	material.set_shader_parameter("pca_frame_rate", 21.0)
	await create_timer(0.35).timeout
	await RenderingServer.frame_post_draw
	var animated := viewport.get_texture().get_image().get_data()
	viewport.queue_free()
	return frozen == still_frozen and frozen != animated

func _check_wave_geometry(source: ShaderMaterial) -> bool:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(256,256)
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport.transparent_bg = true
	viewport.own_world_3d = true
	root.add_child(viewport)
	var material: ShaderMaterial = source.duplicate(false)
	material.set_shader_parameter("pca_frame_rate",0.0)
	material.set_shader_parameter("wave_geometry_enabled",true)
	material.set_shader_parameter("wave_time_scale",0.0)
	material.set_shader_parameter("material_diffuse",Vector4(0.15,0.3,0.35,1.0))
	material.set_shader_parameter("output_gamma",2.2)
	var vertices := PackedVector3Array()
	var shore := PackedFloat32Array()
	var directions := PackedFloat32Array()
	var indices := PackedInt32Array()
	for y in 21:
		for x in 21:
			vertices.append(Vector3(x*20,0,-y*20))
			shore.append_array(PackedFloat32Array([200,200,-20,1000]))
			directions.append_array(PackedFloat32Array([1,0,100,0]))
	for y in 20:
		for x in 20:
			var i := y*21+x
			indices.append_array(PackedInt32Array([i,i+22,i+21,i,i+1,i+22]))
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX]=vertices
	arrays[Mesh.ARRAY_CUSTOM0]=shore
	arrays[Mesh.ARRAY_CUSTOM1]=directions
	arrays[Mesh.ARRAY_INDEX]=indices
	var mesh := ArrayMesh.new()
	var flags := (Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM0_SHIFT) | (Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM1_SHIFT)
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,arrays,[],{},flags)
	mesh.surface_set_material(0,material)
	var instance := MeshInstance3D.new()
	instance.mesh=mesh
	instance.extra_cull_margin=100
	viewport.add_child(instance)
	var camera := Camera3D.new()
	viewport.add_child(camera)
	camera.position=Vector3(200,180,300)
	camera.look_at(Vector3(200,0,-200))
	camera.current=true
	for frame in 5:
		await process_frame
	await RenderingServer.frame_post_draw
	var frozen := viewport.get_texture().get_image().get_data()
	await create_timer(0.15).timeout
	await RenderingServer.frame_post_draw
	var still := viewport.get_texture().get_image().get_data()
	material.set_shader_parameter("wave_time_scale",1.0)
	await create_timer(0.5).timeout
	await RenderingServer.frame_post_draw
	var animated := viewport.get_texture().get_image().get_data()
	var silhouette_changed := false
	for i in range(3,frozen.size(),4):
		if (frozen[i]==0) != (animated[i]==0):
			silhouette_changed=true
			break
	print("WAVE GPU frozen stable=",frozen==still," image changed=",frozen!=animated," silhouette=",silhouette_changed," amplitude=",material.get_shader_parameter("gerstner0"))
	viewport.queue_free()
	return frozen==still and silhouette_changed

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
	assert(report.water.materials[0].technique_index == 5)
	assert(report.water.materials[0].technique == "PCA_U")
	assert(report.water.materials[0].pca_enabled)
	assert(report.water.materials[0].pca_frames == 30)
	assert(report.water.materials[0].grid_spacing == 40.0)
	assert(report.water.materials[0].wave_vertices > 0) # tolerance: authored shoreline must activate geometric waves.
	assert(report.water.materials[0].pca_frame_rate == 21.0)
	var checked := 0
	var authored := 0
	for info in report.water.materials:
		if info.authored:
			authored += 1
	for mesh in map.get_node("StandingWater").get_children():
		var material: ShaderMaterial = mesh.mesh.surface_get_material(0)
		assert(material != null)
		assert(material.get_shader_parameter("terrain_height_tex") != null)
		assert(material.get_shader_parameter("pca_mode") == 2)
		assert(material.get_shader_parameter("gerstner0") == Vector4(50,0.033,0.45,1))
		assert(material.get_shader_parameter("gerstner1") == Vector4(40,0.031,0.5,1))
		assert(material.get_shader_parameter("gerstner2") == Vector4(30,0.029,0.54,1))
		var coefficients: Texture2D = material.get_shader_parameter("pca_coefficients")
		assert(coefficients != null and coefficients.get_width() == 30 and coefficients.get_height() == 10)
		assert(material.get_shader_parameter("pca_tex0") != null)
		assert(material.get_shader_parameter("pca_tex1") != null)
		assert(material.get_shader_parameter("pca_tex2") != null)
		assert(material.get_shader_parameter("material_diffuse") == report.water.materials[checked].color)
		assert(material.get_shader_parameter("bump_uv_scale") == report.water.materials[checked].bump_uv_scale)
		checked += 1
	assert(checked == report.water.areas)
	assert(authored == checked)
	assert(checked > 0) # tolerance: this retail map must exercise standing water.
	if DisplayServer.get_name() != "headless":
		var water_mesh = map.get_node("StandingWater").get_child(0)
		assert(await _check_wave_geometry(water_mesh.mesh.surface_get_material(0)), "Gerstner waves must change the mesh silhouette while frozen time stays stable")
		assert(await _check_pca_animation(water_mesh.mesh.surface_get_material(0)), "PCA coefficients must animate the water while a zero-rate track remains stable")
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

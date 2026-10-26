extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _coating_gpu() -> bool:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(256,256)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var shader := Shader.new()
	shader.code = 'shader_type spatial; render_mode unshaded;\n#include "res://shaders/snow_cover.gdshaderinc"\nvarying vec3 world; varying vec3 normal;\nvoid vertex(){world=(MODEL_MATRIX*vec4(VERTEX,1.0)).xyz;normal=MODEL_NORMAL_MATRIX*NORMAL;}\nvoid fragment(){ALBEDO=snow_cover(vec3(0.12,0.24,0.08),world,normal);}'
	var material := ShaderMaterial.new()
	material.shader = shader
	var box := BoxMesh.new()
	box.size = Vector3(4,2,4)
	box.material = material
	var instance := MeshInstance3D.new()
	instance.mesh = box
	viewport.add_child(instance)
	var camera := Camera3D.new()
	viewport.add_child(camera)
	camera.position = Vector3(5,5,7)
	camera.look_at(Vector3.ZERO)
	camera.current = true
	for frame in 5: await process_frame
	await RenderingServer.frame_post_draw
	var dry := viewport.get_texture().get_image()
	RenderingServer.global_shader_parameter_set("openbfme_snow_cover",1.0)
	for frame in 3: await process_frame
	await RenderingServer.frame_post_draw
	var snowy := viewport.get_texture().get_image()
	RenderingServer.global_shader_parameter_set("openbfme_snow_cover",0.0)
	for frame in 3: await process_frame
	await RenderingServer.frame_post_draw
	var restored := viewport.get_texture().get_image()
	# An up-facing roof is visibly coated; a vertical wall keeps the original material.
	var roof := camera.unproject_position(Vector3(0,1,0))
	var wall := camera.unproject_position(Vector3(0,0,2))
	var rx := int(roof.x)
	var ry := int(roof.y)
	var wx := int(wall.x)
	var wy := int(wall.y)
	var ok := dry.get_data() == restored.get_data() and snowy.get_data() != dry.get_data()
	ok = ok and snowy.get_pixel(rx,ry).r > dry.get_pixel(rx,ry).r # tolerance: the coating must brighten the exposed roof.
	ok = ok and snowy.get_pixel(wx,wy) == dry.get_pixel(wx,wy)
	viewport.queue_free()
	return ok

func _run() -> void:
	create_timer(100).timeout.connect(func(): push_error("Snow weather test timed out"); quit(1))
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(not world.set_snow_weather(true).ok)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	assert(world.setup(fs).ok)
	assert(world.load_map("map mp evendim").ok)
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var terrain = builder.build_map(fs,"map mp evendim",{"markers":false})
	assert(terrain != null)
	root.add_child(terrain)
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.position = Vector3(1500,900,-1000)
	camera.look_at(Vector3(1500,0,-1800))
	camera.current = true
	var before: int = world.get_state_hash()
	var enabled: Dictionary = world.set_snow_weather(true)
	assert(enabled.ok, str(enabled))
	assert(world.snow_weather_enabled())
	assert(enabled.particles > 0) # tolerance: force snow must create precipitation on this non-snow map.
	assert(world.get_state_hash() == before)
	assert(world.get_node("MapSnowfall").emitting)
	for frame in 8: await process_frame
	if DisplayServer.get_name() != "headless":
		await RenderingServer.frame_post_draw
		var capture := OS.get_environment("OPENBFME_SNOW_SCREENSHOT")
		if not capture.is_empty(): assert(root.get_texture().get_image().save_png(capture) == OK)
	assert(world.set_snow_weather(false).ok)
	assert(not world.snow_weather_enabled())
	assert(world.get_state_hash() == before)
	assert(world.get_node_or_null("MapSnowfall") == null)
	if DisplayServer.get_name() != "headless":
		assert(await _coating_gpu(), "Snow must brighten roofs, preserve walls and restore exactly")
	assert(world.set_snow_weather(true).ok)
	# Restoring an authored snow map keeps its native precipitation.
	var native: Dictionary = world.load_map("map mp withered heath")
	print("SNOW NATIVE WEATHER ", native.get("weather",{}))
	assert(native.ok)
	assert(not world.snow_weather_enabled())
	assert(world.set_snow_weather(true).ok)
	assert(world.set_snow_weather(false).ok)
	assert(world.get_node_or_null("MapSnowfall") != null)
	world.clear_game_data(false)
	assert(not world.snow_weather_enabled())
	print("SNOW WEATHER PASS")
	quit()

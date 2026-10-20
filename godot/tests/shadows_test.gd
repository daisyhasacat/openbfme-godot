## GPU integration: a real W3D model casts onto the real terrain shader.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	assert(DisplayServer.get_name() != "headless", "This test needs a GPU")
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var inst = ClassDB.instantiate("W3DInstancer")
	root.add_child(inst)
	assert(inst.setup(fs).ok)
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var atlas = Image.create(4,4,false,Image.FORMAT_RGBA8)
	atlas.fill(Color.WHITE)
	var terrain = builder.build_test_patch({"base_atlas":atlas,"uv0":PackedVector2Array([Vector2(.5,.5),Vector2(.5,.5),Vector2(.5,.5),Vector2(.5,.5)]),"layers":[]})
	root.add_child(terrain)
	var model = inst.add_model("GBWALLN")
	assert(model>=0,str(inst.get_errors()))
	var transform = Transform3D(Basis.IDENTITY.scaled(Vector3.ONE*.035),Vector3(5,0,-5))
	assert(inst.add_instance(model,transform)>=0)
	inst.update_now()
	var light = DirectionalLight3D.new()
	light.rotation_degrees=Vector3(-45,-45,0)
	light.shadow_enabled=false
	light.directional_shadow_max_distance=40
	root.add_child(light)
	var camera = Camera3D.new()
	camera.projection=Camera3D.PROJECTION_ORTHOGONAL
	camera.size=10
	camera.position=Vector3(5,15,-5)
	camera.rotation_degrees=Vector3(-90,0,0)
	root.add_child(camera)
	camera.current=true
	root.size=Vector2i(512,512)
	RenderingServer.global_shader_parameter_set("w3d_shadow_strength",0.0)
	for i in 12: await process_frame
	await RenderingServer.frame_post_draw
	var baseline: Image=root.get_texture().get_image()
	# No occluder shadow: enabling reception must preserve the baked retail colour.
	RenderingServer.global_shader_parameter_set("w3d_shadow_strength",.65)
	for i in 12: await process_frame
	await RenderingServer.frame_post_draw
	var receiving: Image=root.get_texture().get_image()
	var max_error:=0.0
	for y in range(0,512,4):
		for x in range(0,512,4):
			var a=baseline.get_pixel(x,y)
			var b=receiving.get_pixel(x,y)
			max_error=maxf(max_error,maxf(absf(a.r-b.r),maxf(absf(a.g-b.g),absf(a.b-b.b))))
	# tolerance: RGBA16F round trips and final 8-bit output can differ by one byte.
	assert(max_error<=1.0/255.0,"Shadow reception darkens unoccluded surfaces: %f" % max_error)
	light.shadow_enabled=true
	for i in 12: await process_frame
	await RenderingServer.frame_post_draw
	var shadowed: Image=root.get_texture().get_image()
	var darkened:=0
	var unchanged:=0
	for y in range(0,512,2):
		for x in range(0,512,2):
			var before=baseline.get_pixel(x,y).get_luminance()
			var after=shadowed.get_pixel(x,y).get_luminance()
			if before-after>.1: darkened+=1
			if abs(before-after)<.02: unchanged+=1
	assert(darkened>20,"No model shadow on terrain: %d pixels" % darkened)
	assert(unchanged>40000,"Unshadowed terrain lighting changed")
	shadowed.save_png("/tmp/openbfme-shadows.png")
	print("SHADOWS PASS: %d shadowed samples, %d unchanged" % [darkened,unchanged])
	quit()

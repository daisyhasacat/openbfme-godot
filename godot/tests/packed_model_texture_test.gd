## Shadowfax's retail colour is a JPEG with a companion PNG, not a DDS/TGA.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _check_texture(texture: Texture2D, colour: Image, mask: Image) -> void:
	assert(texture != null)
	var image := texture.get_image()
	assert(image != null and not image.is_empty())
	assert(image.get_size() == colour.get_size())
	image.convert(Image.FORMAT_RGBA8)
	for point in [Vector2i(0, 0), Vector2i(32, 32), Vector2i(64, 64)]:
		var c := colour.get_pixelv(point)
		var m := mask.get_pixelv(point)
		# Convert through RGBA8 to match the decoder's stored pixel precision.
		var expected := Image.create(1, 1, false, Image.FORMAT_RGBA8)
		expected.set_pixel(0, 0, Color(c.r * m.r, c.g * m.g, c.b * m.b, m.a))
		assert(image.get_pixelv(point) == expected.get_pixel(0, 0))

func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		quit(77)
		return
	create_timer(90).timeout.connect(func(): push_error("Packed model texture test timed out"); quit(1))
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var stem := "art/compiledtextures/gu/gushadowfaxh"
	var colour := Image.new()
	var mask := Image.new()
	assert(colour.load_jpg_from_buffer(fs.read_file(stem + ".jpg")) == OK)
	assert(mask.load_png_from_buffer(fs.read_file(stem + ".png")) == OK)
	colour.convert(Image.FORMAT_RGBA8)
	mask.convert(Image.FORMAT_RGBA8)
	assert(colour.get_size() == mask.get_size())
	var builder = ClassDB.instantiate("W3DModelBuilder")
	_check_texture(builder.load_texture(fs, "GUShadowFaxH.tga"), colour, mask)
	assert(builder.get_report().errors.is_empty(), str(builder.get_report()))
	assert(str(builder.get_report().texture.packed_texture_stop).begins_with("[S-1482]"))
	var model_node = builder.build_model(fs, "GUGdfHrs_SKN")
	assert(model_node != null)
	assert(builder.get_report().errors.is_empty(), str(builder.get_report()))
	model_node.free()
	var renderer = ClassDB.instantiate("W3DInstancer")
	root.add_child(renderer)
	assert(renderer.setup(fs).ok)
	# The second model variant shares the texture cache and must retain S-1482.
	for name in ["GUGdfHrs_SKN", "GUGdfHrs_SKNL"]:
		var model: int = renderer.add_model(name)
		assert(model >= 0) # tolerance: model handles are nonnegative.
		var report: Dictionary = renderer.get_model_report(model)
		assert(report.errors.is_empty(), str(report))
		var packed_note := false
		for note in report.notes:
			packed_note = packed_note or (str(note).contains("[S-1482] model texture") and str(note).contains("gushadowfaxh.jpg"))
		assert(packed_note, str(report))
		assert(renderer.add_instance(model, Transform3D.IDENTITY) >= 0) # tolerance: instance handles are nonnegative.
	for i in 3:
		await process_frame
	var horse_textures := 0
	for child in renderer.get_children():
		if not child is MultiMeshInstance3D:
			continue
		var mesh: Mesh = child.multimesh.mesh
		for surface in mesh.get_surface_count():
			var material: ShaderMaterial = mesh.surface_get_material(surface)
			var texture: Texture2D = material.get_shader_parameter("w3d_tex0")
			if texture != null and texture.get_width() == colour.get_width() and texture.get_height() == colour.get_height():
				_check_texture(texture, colour, mask)
				horse_textures += 1
	assert(horse_textures == 2)
	print("PACKED MODEL TEXTURE PASS")
	quit()

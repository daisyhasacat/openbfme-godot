## Mesh buffers must remain byte-identical to the pre-bulk-write renderer.
## Needs the retail installations; timings are diagnostic, never an FPS gate.
extends SceneTree
func _initialize(): call_deferred("run")
func run():
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("MESH UPLOAD SKIP: retail installations required")
		quit(77)
		return
	var expected = {
		"EBWallTower": "415c9c388709170af6105dba1d72fc0d733f16afc293ae9daa3e871f18f0cf08",
		"IBWallTower": "5bdef4879e2b17571ef81a4c698dd4dc78a1f1478a17e3d2aeda5e6df8ea29fa",
		"DBFTOWER_D3": "e7a625bd88f482f021c6ad3e3583fae2ff4219ab01b942ccc60b470a0475c69c",
		"AUBOMSHIP_A": "db887403b62bfa1ad7ca3421f15c055b6734a1983dc2c2c0e82260f7a6e20f63",
	}
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	for model in ["EBWallTower", "IBWallTower", "DBFTOWER_D3", "AUBOMSHIP_A"]:
		var inst = ClassDB.instantiate("W3DInstancer")
		root.add_child(inst)
		assert(inst.setup(fs).ok)
		var start = Time.get_ticks_usec()
		var id = inst.add_model(model)
		var elapsed = Time.get_ticks_usec() - start
		assert(inst.get_model_report(id).ok)
		var data = []
		for node in inst.get_children():
			if node is MultiMeshInstance3D:
				var mesh = node.multimesh.mesh
				for surface in mesh.get_surface_count(): data.append(mesh.surface_get_arrays(surface))
		assert(var_to_bytes(data).hex_encode().sha256_text() == expected[model], model)
		print("MESH UPLOAD ", model, " ", elapsed, " us")
		inst.free()
	palette_bindings(fs)
	print("MESH UPLOAD PASS")
	quit()

func check_palettes(inst: Node, expected: Texture2D) -> void:
	for node in inst.get_children():
		if node is MultiMeshInstance3D:
			var mesh = node.multimesh.mesh
			for surface in mesh.get_surface_count():
				assert(mesh.surface_get_material(surface).get_shader_parameter("w3d_palette") == expected)

func palette_bindings(fs: RefCounted) -> void:
	var inst = ClassDB.instantiate("W3DInstancer")
	root.add_child(inst)
	assert(inst.setup(fs).ok)
	var first = inst.add_model("EBWallTower")
	var instance = inst.add_instance(first, Transform3D.IDENTITY, "", 0.0, 1.0)
	inst.update_now()
	var initial: Texture2D = inst.get_child(0).multimesh.mesh.surface_get_material(0).get_shader_parameter("w3d_palette")
	assert(is_instance_valid(initial))
	check_palettes(inst, initial)
	# Loading another model without instances cannot require palette growth.
	inst.add_model("IBWallTower")
	inst.update_now()
	check_palettes(inst, initial)
	var pose: PackedFloat32Array = inst.get_instance_palette(instance)
	for i in 100: inst.add_instance(first, Transform3D.IDENTITY, "", 0.0, 1.0)
	inst.update_now()
	var grown: Texture2D = inst.get_child(0).multimesh.mesh.surface_get_material(0).get_shader_parameter("w3d_palette")
	assert(grown != initial)
	check_palettes(inst, grown)
	assert(inst.get_instance_palette(instance) == pose)
	inst.free()

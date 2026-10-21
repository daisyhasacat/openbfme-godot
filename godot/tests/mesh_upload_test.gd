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
	unchanged_render_state(fs)
	animated_sort_preservation(fs)
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

func packed_buffers(inst: Node) -> Array:
	var buffers: Array = []
	for node in inst.get_children():
		if node is MultiMeshInstance3D:
			buffers.append(node.multimesh.buffer)
	return buffers

func unchanged_render_state(fs: RefCounted) -> void:
	var inst = ClassDB.instantiate("W3DInstancer")
	root.add_child(inst)
	assert(inst.setup(fs).ok)
	inst.set_auto_update(false)
	inst.set_playing(false)
	inst.set_sort_camera(Transform3D.IDENTITY)
	var model: int = inst.add_model("EBWallTower")
	var id: int = inst.add_instance(model, Transform3D.IDENTITY, "", 0.0, 1.0)
	inst.update_now()
	var palette: PackedFloat32Array = inst.get_instance_palette(id)
	var buffers := packed_buffers(inst)
	for step in 4:
		inst.set_instance_transform(id, Transform3D.IDENTITY)
		inst.set_instance_house_color(id, Color(0, 0, 0, 0))
		inst.set_instance_infantry_light(id, false)
		# A new explicit request can still evaluate to the identical bind pose.
		assert(inst.set_instance_pose(id, "", float(step)))
		inst.update_now()
		assert(inst.get_stats().pose_evaluations == 1)
		assert(inst.get_stats().buffer_writes == 0)
		assert(inst.get_stats().buffer_uploads == 0)
		assert(inst.get_stats().palette_uploads == 0)
		assert(inst.get_instance_palette(id) == palette)
		assert(packed_buffers(inst) == buffers)
	# Actual changes still reach the packed GPU data, then become idle again.
	for change in [func(): inst.set_instance_transform(id, Transform3D(Basis.IDENTITY, Vector3.ONE)),
		func(): inst.set_instance_house_color(id, Color(1, 0, 0, 1)),
		func(): inst.set_instance_infantry_light(id, true),
		func(): inst.set_instance_opacity(id, 0.5)]:
		var before := packed_buffers(inst)
		change.call()
		inst.update_now()
		assert(inst.get_stats().buffer_writes == 1)
		assert(packed_buffers(inst) != before)
		inst.update_now()
		assert(inst.get_stats().buffer_writes == 0)
	print("UNCHANGED RENDER STATE: 4 pose evaluations, 0 model-buffer rebuilds or palette uploads")
	inst.free()

func animated_sort_preservation(fs: RefCounted) -> void:
	var ini: String = fs.read_file("data\\ini\\object\\goodfaction\\units\\men\\gondorfighter.ini").get_string_from_utf8()
	var name := RetailIni.find_default_model(ini, "GondorFighter")
	assert(not name.is_empty())
	var instances: Array = []
	var ids: Array = []
	var other_ids: Array = []
	for index in 2:
		var inst = ClassDB.instantiate("W3DInstancer")
		root.add_child(inst)
		assert(inst.setup(fs).ok)
		inst.set_auto_update(false)
		inst.set_playing(false)
		inst.set_sort_camera(Transform3D.IDENTITY)
		var model: int = inst.add_model(name)
		ids.append(inst.add_instance(model, Transform3D.IDENTITY, "GUManMocap_IDLB", 0.0, 1.0))
		other_ids.append(inst.add_instance(model, Transform3D.IDENTITY, "GUManMocap_IDLB", 0.5, 1.0))
		inst.update_now()
		instances.append(inst)
	var initial: PackedFloat32Array = instances[0].get_instance_palette(ids[0])
	for step in range(1, 11):
		# Force the reference to rebuild its buffers, as the old pose path did.
		instances[1].set_instance_transform(ids[1], Transform3D(Basis.IDENTITY, Vector3.ONE))
		for index in 2:
			instances[index].set_instance_transform(ids[index], Transform3D.IDENTITY)
			instances[index].set_global_time(0.07 * step)
			instances[index].update_now()
		assert(instances[0].get_stats().buffer_writes == 1)
		assert(instances[1].get_stats().buffer_writes == 1)
		assert(instances[0].get_stats().palette_uploads == 1)
		assert(packed_buffers(instances[0]) == packed_buffers(instances[1]))
		assert(instances[0].get_instance_palette(ids[0]) == instances[1].get_instance_palette(ids[1]))
		assert(instances[0].get_instance_palette(other_ids[0]) == instances[1].get_instance_palette(other_ids[1]))
	assert(instances[0].get_instance_palette(ids[0]) != initial)
	print("ANIMATED SORT PRESERVATION: moving transparent pivots rebuild; palettes and buffers match forced rebuilds")
	for inst in instances: inst.free()

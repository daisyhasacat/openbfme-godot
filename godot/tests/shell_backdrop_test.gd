## Retail BFME2 shell scene animates without loading a gameplay world.
extends SceneTree
func _initialize() -> void:
	call_deferred("_run")
func _run() -> void:
	var game = load("res://scenes/game.tscn").instantiate()
	root.add_child(game)
	await create_timer(3.0).timeout
	var shell = game.get("_shell")
	assert(shell != null)
	var first: Dictionary = shell.get_backdrop_state()
	assert(first.backdrop_image == "Shell02_01", str(first))
	var scene
	for instance in shell.find_children("*", "W3DInstancer", true, false):
		if not instance.get_shell_scene_state().is_empty():
			scene = instance
			break
	assert(scene != null, "authored shell scene was not loaded")
	var authored: Dictionary = scene.get_shell_scene_state()
	assert(authored.shell_map.to_lower() == "maps\\shellmap1\\shellmap1.map")
	assert(authored.fog_start == 200.0 and authored.fog_end == 3000.0)
	assert(authored.water_areas == 1 and authored.reflection_on and authored.reflection_plane == 37.0)
	assert(authored.particle_systems == 6)
	assert(authored.named_cameras == 8)
	assert(authored.camera_name == "CameraEnd" and authored.camera_moves == 2)
	assert(authored.fade_starts == 1, "authored opening fade did not run")
	assert(authored.unverified == ["[S-5050] shell scene emissive glow pass is not ported"])
	var view: SubViewport = scene.get_parent()
	assert(view.size == Vector2i(shell.get_viewport_rect().size), "shell scene was rendered with the menu's 4:3 stage aspect")
	# tolerance: the authored camera must remain above the reflection plane; exact height changes with animation.
	assert(view.get_camera_3d().global_position.y > authored.reflection_plane, "shell actor was not placed relative to terrain")
	var particles = scene.get_node("ShellMapParticles")
	assert(not particles.get_soft_particles())
	# tolerance: particle counts vary with elapsed client time; the six authored systems must emit visible particles.
	assert(particles.get_stats().particles > 0)
	for mesh in scene.find_children("*", "MultiMeshInstance3D", true, false):
		var material = mesh.material_override
		if material is ShaderMaterial and material.shader.code.contains("w3d_amb_obj"):
			assert(not material.shader.code.contains("global uniform vec3 w3d_amb_obj;"), "shell lighting leaked into gameplay globals")

	await create_timer(0.5).timeout
	var second: Dictionary = shell.get_backdrop_state()
	# tolerance: elapsed render time determines the number of advanced animation frames.
	assert(second.animation_frame > first.animation_frame, "shell animation did not advance")
	assert(shell.get_report().view3d_errors.is_empty(), str(shell.get_report().view3d_errors))
	if DisplayServer.get_name() != "headless":
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png("/tmp/openbfme-shell-backdrop.png")
	# Wait for the map's first transition: the fade must cover the camera cut.
	var cut_deadline := Time.get_ticks_msec() + 25000
	while Time.get_ticks_msec() < cut_deadline:
		var effect: Dictionary = scene.get_shell_scene_state()
		if effect.fade_starts == 2 and effect.fade_add == 1.0:
			break
		await process_frame
	var white: Dictionary = scene.get_shell_scene_state()
	assert(white.fade_starts == 2 and white.fade_add == 1.0, "camera transition never faded to white")
	await RenderingServer.frame_post_draw
	if DisplayServer.get_name() != "headless":
		var picture: Image = root.get_texture().get_image()
		assert(picture.get_pixel(picture.get_width() / 8, picture.get_height() / 2) == Color.WHITE, "fade was not composited into the backdrop")
		picture.save_png("/tmp/openbfme-shell-white-fade.png")
	var recover_deadline := Time.get_ticks_msec() + 6000
	while scene.get_shell_scene_state().fade_add != 0.0 and Time.get_ticks_msec() < recover_deadline:
		await process_frame
	assert(scene.get_shell_scene_state().fade_add == 0.0, "camera transition stayed white")
	assert(scene.get_shell_scene_state().fade_starts == 2)
	assert(scene.get_shell_scene_state().camera_name == "CameraEndB")
	assert(scene.get_shell_scene_state().camera_moves == 4)
	shell.shell_push("Options.apt")
	await create_timer(0.5).timeout
	assert(shell.get_backdrop_state().backdrop_image == "ShellMapLowLOD", "settings backdrop changed")
	shell.shell_hide_background()
	shell.shell_show_shell_map(false)
	await create_timer(0.2).timeout
	assert(shell.get_backdrop_state().backdrop_image == "", "shell backdrop survived game start")
	assert(shell.get_backdrop_state().background_commands == 0, "front-end overlay survived game start")
	for child in shell.get_children():
		if child is SubViewport and child.name == "ShellMapReflection":
			assert(child.render_target_update_mode == SubViewport.UPDATE_DISABLED)
	shell.shell_fscommand("SetBackground", "fadein")
	await create_timer(0.5).timeout
	assert(shell.get_backdrop_state().background_mode == 1, "settings background did not restore")
	print("SHELL BACKDROP PASS frames ",first.animation_frame," -> ",second.animation_frame)
	game.queue_free()
	await process_frame
	quit(0)

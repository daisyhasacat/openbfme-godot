## Installed BFME2 data: real tutorial paths, cell classification, presentation purity and toggles.
extends SceneTree

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	create_timer(180).timeout.connect(func(): printerr("NAVIGATION DEBUG TIMEOUT"); quit(1))
	var game = load("res://scripts/game.gd").new()
	game._intro = "off"
	assert(not game._debug_navigation)
	game._debug_navigation = true
	root.add_child(game)
	while game._campaign_flow == null or game._state not in [game.State.MENU, game.State.FAILED]: await process_frame
	assert(game._state == game.State.MENU, game._failed)
	await game._wait_main_ready()
	var world: Node = game._world
	assert(not world.debug_navigation(0, 0, true).ok)
	world.set_logic_thread(false)
	game._campaign_flow.hooks = true
	game._campaign_flow.start_tutorial("Basic")
	while game._state not in [game.State.PLAYING, game.State.FAILED]: await process_frame
	assert(game._state == game.State.PLAYING, game._failed)
	world.set_auto_advance(false)
	game._campaign_flow._cine.set_process(false)
	var overlay: Node = game.get_node("NavigationDebug")
	assert(overlay.paths_enabled and overlay.terrain_enabled)
	for i in 15: world.advance(0.2)
	var unit: Dictionary = world.debug_script_unit("Fighters 01")
	var before = world.get_state_hash()
	var data: Dictionary = world.debug_navigation(500, 4470, true)
	assert(data.ok)
	assert(data.width == 60 and data.height == 60)
	assert(data.vertices.size() == 61 * 61)
	assert(data.types.size() == 3600)
	assert(data.pinched.size() == 3600)
	var found := false
	for route in data.routes:
		if route.id == unit.id:
			found = true
			assert(route.has_path)
			assert(not route.points.is_empty())
			var object: Dictionary = world.get_object(unit.id)
			assert(route.points[0] == Vector3(object.x, maxf(object.z, world.get_ground_height(object.x, object.y)) + 3.0, -object.y))
	assert(found, "The moving tutorial horde must have its existing path displayed")
	var sample: Dictionary = world.debug_pathfind_cell(205, 4175)
	assert(data.types[0] == sample.type)
	assert(bool(data.pinched[0]) == sample.pinched)
	for i in 3: overlay.apply_snapshot(data)
	assert(overlay._paths.mesh.get_surface_count() == 1)
	assert(overlay._terrain.mesh.get_surface_count() == 1)
	assert(overlay._terrain.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX].size() == 3600 * 6)
	assert(world.get_state_hash() == before, "Rendering debug geometry must not change simulation state")
	overlay._panel.get_child(0).button_pressed = false
	assert(overlay._paths.mesh == null)
	assert(overlay._terrain.mesh != null)
	overlay._panel.get_child(1).button_pressed = false
	assert(overlay._paths.mesh == null and overlay._terrain.mesh == null)
	assert(not world.debug_navigation(0, 0, false).has("vertices"))
	var edge: Dictionary = world.debug_navigation(-100, -100, true)
	assert(edge.width == 30 and edge.height == 30)
	overlay.paths_enabled = true
	overlay.apply_snapshot({"routes": []})
	assert(overlay._paths.mesh == null)
	overlay.set_covered(true)
	assert(not overlay.visible and not overlay._ui.visible)
	overlay.set_covered(false)
	assert(overlay.visible and overlay._ui.visible)
	if OS.get_environment("NAVIGATION_DEBUG_SHOT") != "":
		overlay.terrain_enabled = true
		overlay.apply_snapshot(data)
		game._hud.set_process(false)
		game._camera.projection = Camera3D.PROJECTION_ORTHOGONAL
		game._camera.size = 900
		game._camera.position = Vector3(500, 1800, -4470)
		game._camera.look_at(Vector3(500, 100, -4470), Vector3(0, 0, -1))
		await process_frame
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png(OS.get_environment("NAVIGATION_DEBUG_SHOT"))
	game.free()
	print("NAVIGATION DEBUG PASS")
	quit()

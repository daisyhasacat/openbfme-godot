## Two real HUDs on one retail world. No second simulation or synthetic orders.
extends SceneTree

func _initialize() -> void:
	create_timer(120).timeout.connect(func(): push_error("Second view timed out"); quit(1))
	call_deferred("run")

func run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("second_view_native_test: SKIP (ROTWK_INSTALL and BFME2_INSTALL required)")
		quit(77)
		return
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var slots: Array = []
	for i in 8:
		slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	slots[0] = {"state": 6, "name": "Westfold", "accepted": true, "color": 0, "start_pos": 0, "player_template": 3, "team": 0}
	slots[1] = {"state": 4, "name": "Mordor", "accepted": true, "color": 1, "start_pos": 1, "player_template": 6, "team": 1}
	var prepared: Dictionary = world.prepare_new_game({"mode": "skirmish", "difficulty": 1,
		"map": "maps/map mp fall back 4p/map mp fall back 4p.map", "map_crc": 0,
		"map_size": 0, "map_mask": 0, "seed": 777, "starting_cash": 1500, "slots": slots})
	assert(prepared.ok, str(prepared))
	var started: Dictionary = world.start_new_game({"logic_thread": false})
	assert(started.ok, str(started))
	var main_camera := Camera3D.new()
	root.add_child(main_camera)
	main_camera.make_current()
	var primary = ClassDB.instantiate("InGameHudNode")
	root.add_child(primary)
	var primary_boot: Dictionary = primary.setup(fs, world, main_camera, started.start.local_player,
		{"camera_start": started.start.camera_start})
	assert(primary_boot.ok, str(primary_boot))
	var view := Window.new()
	view.hide()
	view.force_native = true
	view.size = Vector2i(960, 540)
	view.world_3d = main_camera.get_world_3d()
	root.add_child(view)
	var second_camera := Camera3D.new()
	view.add_child(second_camera)
	second_camera.make_current()
	var secondary = ClassDB.instantiate("InGameHudNode")
	view.add_child(secondary)
	var start: Vector3 = started.start.camera_start + Vector3(400, 0, 0)
	var secondary_boot: Dictionary = secondary.setup(fs, world, second_camera, started.start.local_player,
		{"camera_start": start, "secondary_view": true})
	assert(secondary_boot.ok, str(secondary_boot))
	secondary.link_secondary_view(primary)
	secondary.set_view_active(false)
	var position: Vector2 = primary.get_camera().position
	secondary.camera_pan(Vector2(100, 0))
	assert(primary.get_camera().position == position)
	var units: Array[int] = []
	for object in world.get_player_objects(started.start.local_player):
		if not object.structure:
			units.append(object.id)
	assert(units.slice(0, 2).size() == 2)
	var id: int = units[0]
	var other_id: int = units[1]
	assert(primary.select_controller_group(PackedInt64Array([id])) == 1)
	for i in 4: await process_frame
	assert(primary.get_selection() == [id])
	assert(secondary.get_selection() == [])
	assert(secondary.get_adaptive_minimap().ready)
	assert(secondary.is_ready())
	# A command key in the second viewport must reach that HUD exactly once.
	secondary.set_view_active(true)
	primary.set_view_active(false)
	assert(secondary.select_controller_group(PackedInt64Array([other_id])) == 1)
	for i in 4: await process_frame
	world.advance(0.2)
	assert(secondary.get_selection() == [other_id])
	assert(primary.get_selection() == [id])
	assert(secondary.get_logic_selection() == [other_id])
	var before: int = secondary.get_state().keys.key_events
	var main_before: int = primary.get_state().keys.key_events
	var key := InputEventKey.new()
	key.keycode = KEY_S
	key.physical_keycode = KEY_S
	key.pressed = true
	view.push_input(key)
	key = key.duplicate()
	key.pressed = false
	view.push_input(key)
	for i in 4: await process_frame
	assert(secondary.get_state().keys.key_events == before + 2)
	assert(primary.get_state().keys.key_events == main_before)
	secondary.deselect_all()
	for i in 4: await process_frame
	assert(primary.get_selection() == [id])
	assert(secondary.get_selection() == [])
	# Focus restores the destination group, including an empty selection.
	primary.set_view_active(true)
	secondary.set_view_active(false)
	world.advance(0.2)
	assert(primary.get_logic_selection() == [id])
	assert(secondary.get_selection() == [])
	secondary.set_view_active(true)
	primary.set_view_active(false)
	world.advance(0.2)
	assert(secondary.get_logic_selection() == [])
	assert(primary.get_selection() == [id])
	# Switching away mid-drag must cancel the old viewport's drag selection.
	primary.set_view_active(true)
	secondary.set_view_active(false)
	primary.inject_mouse_button(1, true, Vector2(600, 200), false)
	primary.inject_mouse_move(Vector2(700, 300))
	for i in 4: await process_frame
	assert(primary.get_state().drag_box.selecting)
	primary.set_view_active(false)
	secondary.set_view_active(true)
	primary.set_view_active(true)
	secondary.set_view_active(false)
	primary.inject_mouse_move(Vector2(750, 350))
	for i in 4: await process_frame
	assert(not primary.get_state().drag_box.selecting)
	# The passive HUD must not clear the active view's ground-targeting decal.
	world.set_shroud_drawn(false)
	var hero: int = world.create_object("ElvenElrond", started.start.local_player_index, 2000, 1800, 0)
	world.gain_hero_levels(hero, 10)
	world.ready_object_powers(hero)
	primary.camera_look_at(Vector2(2000, 1800))
	assert(primary.select_object_with_message(hero))
	for i in 20:
		world.advance(1.0 / 30.0)
		await process_frame
	assert(primary.press_command_button("Command_SpecialPowerSpawnWhirlwind"))
	primary.inject_mouse_move(primary.world_to_pixel(Vector2(2050, 1800)))
	for i in 4: await process_frame
	var cursor: Dictionary = primary.get_spellbook_state().radius_cursor
	assert(cursor.visible and cursor.drawn, str(cursor))
	assert(cursor.size == 140.0)
	if DisplayServer.get_name() != "headless":
		await RenderingServer.frame_post_draw
		assert(root.get_texture().get_image().save_png("/tmp/openbfme-second-main.png") == OK)
		view.show()
		await RenderingServer.frame_post_draw
		assert(view.get_texture().get_image().save_png("/tmp/openbfme-second-view.png") == OK)
	primary.inject_mouse_button(2, true, Vector2(600, 250), false)
	primary.inject_mouse_button(2, false, Vector2(600, 250), false)
	for i in 4: await process_frame
	# Script control remains owned by the main HUD and reaches both surfaces.
	primary.script_input(false)
	assert(not secondary.get_state().keys.input_enabled)
	primary.script_input(true)
	primary.set_view_active(true)
	secondary.set_view_active(false)
	secondary.free()
	view.free()
	primary.free()
	main_camera.free()
	world.free()
	print("second_view_native_test: PASS")
	quit()

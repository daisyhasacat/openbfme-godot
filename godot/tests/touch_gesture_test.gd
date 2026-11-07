## Enhanced finger/Pencil controls on a real world, with no iPad required.
extends SceneTree

func _initialize() -> void:
	create_timer(120).timeout.connect(func(): push_error("Touch gesture test timed out"); quit(1))
	call_deferred("run")

func touch(index: int, pressed: bool, position: Vector2, canceled := false) -> void:
	var event := InputEventScreenTouch.new()
	event.index = index
	event.pressed = pressed
	event.position = position
	event.canceled = canceled
	root.push_input(event, true)

func drag(index: int, position: Vector2) -> void:
	var event := InputEventScreenDrag.new()
	event.index = index
	event.position = position
	root.push_input(event, true)

func pencil(pressed: bool, position: Vector2, canceled := false) -> void:
	var event := InputEventMouseButton.new()
	event.device = 1001
	event.button_index = MOUSE_BUTTON_LEFT
	event.button_mask = MOUSE_BUTTON_MASK_LEFT if pressed else 0
	event.pressed = pressed
	event.position = position
	event.canceled = canceled
	root.push_input(event, true)

func pencil_drag(position: Vector2) -> void:
	var event := InputEventMouseMotion.new()
	event.device = 1001
	event.button_mask = MOUSE_BUTTON_MASK_LEFT
	event.position = position
	root.push_input(event, true)

func run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("TOUCH GESTURE SKIP: ROTWK_INSTALL and BFME2_INSTALL required")
		quit(77)
		return
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.load_map("map mp fall back 4p", {"slots": [{"player": "Player_1", "faction": "FactionMen", "human": true}]}).ok)
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs, world, camera, "Player_1", {"camera_start": Vector3(1800, 0, -1800)}).ok)
	await process_frame
	var before: Vector2 = hud.get_camera().position
	# Indirect trackpad events use ordinary desktop mouse semantics, not finger panning.
	var click := InputEventMouseButton.new()
	click.position = Vector2(500, 200)
	click.button_index = MOUSE_BUTTON_LEFT
	click.button_mask = MOUSE_BUTTON_MASK_LEFT
	click.pressed = true
	root.push_input(click.duplicate(), true)
	var pointer := InputEventMouseMotion.new()
	pointer.position = Vector2(600, 260)
	pointer.relative = Vector2(100, 60)
	pointer.button_mask = MOUSE_BUTTON_MASK_LEFT
	root.push_input(pointer, true)
	for i in 3: await process_frame
	assert(hud.get_state().drag_box.selecting)
	assert(hud.get_camera().position == before)
	click.position = pointer.position
	click.pressed = false
	click.button_mask = 0
	root.push_input(click, true)
	for i in 3: await process_frame
	assert(not hud.get_state().drag_box.selecting)
	touch(0, true, Vector2(500, 200))
	drag(0, Vector2(505, 200))
	assert(hud.get_camera().position == before)
	drag(0, Vector2(540, 200))
	assert(hud.get_camera().position != before)
	touch(0, false, Vector2(540, 200))
	for i in 3: await process_frame
	assert(not hud.get_state().drag_box.selecting)
	assert(hud.get_selection() == [])
	assert(hud.get_state().keys.touch_orders == 0)
	# World-owned gestures clean up even when released over a HUD panel.
	Engine.set_meta("touch_toolbar_rect", Rect2(620, 150, 150, 180))
	touch(0, true, Vector2(500, 200))
	drag(0, Vector2(650, 200))
	touch(0, false, Vector2(650, 200))
	Engine.remove_meta("touch_toolbar_rect")
	hud.camera_set_height(240.0)
	touch(0, true, Vector2(500, 200))
	drag(0, Vector2(540, 200))
	assert(hud.get_camera().height_above_ground == 240.0)
	touch(0, false, Vector2(540, 200))
	# Distance ratios, not frame rate or a device-specific gesture event, zoom.
	hud.camera_set_height(240.0)
	touch(0, true, Vector2(500, 200))
	touch(1, true, Vector2(600, 200))
	drag(1, Vector2(700, 200))
	assert(hud.get_camera().height_above_ground == 120.0)
	drag(1, Vector2(600, 200))
	assert(hud.get_camera().height_above_ground == 240.0)
	touch(1, false, Vector2(600, 200))
	touch(0, false, Vector2(500, 200))
	assert(hud.get_state().keys.touch_orders == 0)
	# Twist uses the shortest signed angle and ignores small finger jitter.
	hud.camera_set_angle_pitch(0.0, hud.get_camera().pitch)
	touch(0, true, Vector2(500, 200))
	touch(1, true, Vector2(600, 200))
	drag(1, Vector2(600, 201))
	assert(hud.get_camera().angle == 0.0)
	drag(1, Vector2(500, 300))
	# tolerance: camera angles are stored as float32; compare a quarter turn within float rounding.
	assert(absf(hud.get_camera().angle + PI / 2.0) < 0.000001)
	drag(1, Vector2(600, 200))
	# tolerance: summing the reverse float32 angle can retain a rounding residual.
	assert(absf(hud.get_camera().angle) < 0.000001)
	touch(1, false, Vector2(600, 200))
	touch(0, false, Vector2(500, 200))
	assert(not hud.get_state().drag_box.selecting)
	assert(hud.get_state().keys.touch_orders == 0)
	hud.hold_input(true)
	touch(0, true, Vector2(500, 200))
	drag(0, Vector2(540, 200))
	touch(0, false, Vector2(540, 200))
	assert(hud.get_state().keys.pending == 3)
	hud.notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT)
	assert(hud.get_state().keys.pending == 0)
	hud.hold_input(false)
	for i in 3: await process_frame
	assert(hud.get_state().keys.touch_orders == 0)
	# Pencil micro-motion stays a tap; a deliberate drag starts area selection.
	before = hud.get_camera().position
	pencil(true, Vector2(500, 180))
	pencil_drag(Vector2(505, 180))
	for i in 3: await process_frame
	assert(not hud.get_state().drag_box.selecting)
	pencil_drag(Vector2(600, 260))
	for i in 3: await process_frame
	assert(hud.get_state().drag_box.selecting)
	assert(hud.get_camera().position == before)
	Engine.set_meta("touch_toolbar_rect", Rect2(580, 230, 150, 180))
	pencil(false, Vector2(600, 260), true)
	Engine.remove_meta("touch_toolbar_rect")
	for i in 3: await process_frame
	assert(not hud.get_state().drag_box.selecting)
	assert(hud.get_selection() == [])
	# A world stroke ending over the native HUD must also clear selection drag.
	pencil(true, Vector2(500, 180))
	pencil_drag(Vector2(600, 260))
	for i in 3: await process_frame
	assert(hud.get_state().drag_box.selecting)
	pencil(false, Vector2(100, 700))
	for i in 3: await process_frame
	assert(not hud.get_state().drag_box.selecting)
	assert(hud.get_selection() == [])
	# Cancel/focus loss must never turn the remaining finger into a click.
	touch(0, true, Vector2(500, 200))
	hud.notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT)
	touch(0, false, Vector2(500, 200))
	assert(hud.get_state().keys.touch_orders == 0)
	hud.free()
	camera.free()
	world.free()
	print("TOUCH GESTURE PASS")
	quit()

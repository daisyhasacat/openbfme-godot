## Presentation-only native window sharing the live world and its visibility.
extends CanvasLayer

var source: Camera3D
var game: Node
var button: Button
var window: Window
var camera: Camera3D
var world: Node
var restore_culling := true


func _ready() -> void:
	layer = 110
	button = Button.new()
	button.text = "Pop out view"
	button.tooltip_text = "Open another camera. Drag to pan; scroll to zoom."
	button.focus_mode = Control.FOCUS_NONE
	button.position = Vector2(16, 48)
	button.pressed.connect(open_view)
	add_child(button)
	process_priority = 110


func _process(_delta: float) -> void:
	var playing: bool = game._state == game.State.PLAYING and is_instance_valid(game._hud)
	button.visible = playing and DisplayServer.get_name() != "headless" and not OS.has_feature("mobile")
	# AdaptiveHUD publishes its rectangles earlier each frame. Include this
	# button in the native HUD's input exclusion list before the next input pass.
	if button.visible:
		var rects: Array = Engine.get_meta("adaptive_hud_rects", [])
		rects.append(button.get_global_rect())
		Engine.set_meta("adaptive_hud_rects", rects)
	if is_instance_valid(window):
		if not playing or world != game._world:
			close_view()
		else:
			# Keep view directions parallel for the shared transparent batches' depth order.
			camera.global_basis = source.global_basis


func open_view() -> void:
	if is_instance_valid(window):
		window.grab_focus()
		return
	world = game._world
	restore_culling = world.get_perf3_client()
	# The caster PiP also owns an extra frustum. Keep all poses current while
	# this additional view exists so neither offscreen camera freezes units.
	world.set_perf3_client(false)
	window = Window.new()
	window.hide()
	window.title = "OpenBFME — Second view"
	window.force_native = true
	window.size = Vector2i(960, 540)
	window.min_size = Vector2i(320, 180)
	window.world_3d = source.get_world_3d()
	window.close_requested.connect(close_view)
	window.window_input.connect(_view_input)
	add_child(window)
	camera = source.duplicate(0) as Camera3D
	window.add_child(camera)
	camera.global_transform = source.global_transform
	camera.global_position += source.global_basis.x * 400.0
	camera.make_current()
	window.show()


func _view_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and event.button_mask & MOUSE_BUTTON_MASK_LEFT:
		var right := camera.global_basis.x
		var forward := -camera.global_basis.z
		forward.y = 0
		var scale := maxf(0.5, camera.global_position.y / 500.0)
		camera.global_position += (-right * event.relative.x + forward.normalized() * event.relative.y) * scale
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			camera.global_position -= camera.global_basis.z * 60.0
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			camera.global_position += camera.global_basis.z * 60.0


func close_view() -> void:
	if is_instance_valid(world):
		world.set_perf3_client(restore_culling)
	world = null
	if is_instance_valid(window):
		window.hide()
		window.queue_free()
	window = null
	camera = null


func _exit_tree() -> void:
	close_view()

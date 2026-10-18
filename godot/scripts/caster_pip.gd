## A second presentation camera on the same world: no second simulation or separate fog state.
extends PanelContainer

signal enabled_changed(value: bool)
signal swapped

var enabled := false
var suspended := false
var pinned := false
var target := Vector3.ZERO
var distance := 600.0
var hud: Node
var world: Node
var main_camera: Camera3D
var camera: Camera3D
var viewport: SubViewport
var picture: TextureRect
var controls: HBoxContainer
var label: Label
var instancers: Array[Node] = []


func configure(game_world: Node, camera_hud: Node, source_camera: Camera3D) -> void:
	world = game_world
	hud = camera_hud
	main_camera = source_camera
	var style := StyleBoxFlat.new()
	style.bg_color = Color("10191eef")
	style.border_color = Color("d7bd7c")
	style.set_border_width_all(1)
	style.set_content_margin_all(6)
	add_theme_stylebox_override("panel", style)
	var body := VBoxContainer.new()
	add_child(body)
	label = Label.new()
	label.text = "PINNED VIEW · shared vision"
	label.add_theme_color_override("font_color", Color("d7bd7c"))
	label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	body.add_child(label)
	controls = HBoxContainer.new()
	body.add_child(controls)
	for spec in [["Pin here [Shift+F7]", pin_here], ["Swap", swap_views], ["Close", func(): set_enabled(false)]]:
		var button := Button.new()
		button.text = spec[0]
		button.focus_mode = Control.FOCUS_NONE
		button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		button.pressed.connect(spec[1])
		controls.add_child(button)
	viewport = SubViewport.new()
	viewport.world_3d = main_camera.get_world_3d()
	viewport.gui_disable_input = true
	viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	add_child(viewport)
	camera = Camera3D.new()
	viewport.add_child(camera)
	camera.make_current()
	picture = TextureRect.new()
	picture.texture = viewport.get_texture()
	picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	picture.stretch_mode = TextureRect.STRETCH_SCALE
	picture.size_flags_vertical = Control.SIZE_EXPAND_FILL
	picture.tooltip_text = "Double-click to swap views. Wheel zooms this view only. Same vision and view angle as the main camera."
	body.add_child(picture)
	picture.gui_input.connect(_picture_input)
	for node in world.get_children():
		if node.has_method("set_extra_pose_frustum"):
			instancers.append(node)
	visible = false
	process_priority = 100


func set_enabled(value: bool) -> void:
	if value and not pinned:
		pin_here()
	enabled = value
	_apply_visibility()
	enabled_changed.emit(value)


func set_suspended(value: bool) -> void:
	suspended = value
	_apply_visibility()


func _apply_visibility() -> void:
	visible = enabled and not suspended
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS if visible else SubViewport.UPDATE_DISABLED
	if not visible:
		_clear_frustum()


func set_clean(value: bool) -> void:
	controls.visible = not value


func pin_here() -> void:
	var view: Dictionary = hud.get_camera()
	target = view.target
	distance = view.eye.distance_to(view.target)
	pinned = true


func swap_views() -> void:
	var previous: Vector3 = hud.get_camera().target
	hud.camera_look_at(Vector2(target.x, -target.z))
	target = previous
	swapped.emit()


func _picture_input(event: InputEvent) -> void:
	if not event is InputEventMouseButton or not event.pressed:
		return
	if event.button_index == MOUSE_BUTTON_LEFT and event.double_click:
		swap_views()
	elif event.button_index == MOUSE_BUTTON_WHEEL_UP:
		distance /= 1.15
	elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
		distance *= 1.15
	distance = clampf(distance, 100, maxf(200, main_camera.far * 0.75))
	accept_event()


func over_panel(point: Vector2) -> bool:
	return visible and get_global_rect().has_point(point)


func preferences() -> Dictionary:
	return {"enabled": enabled, "pinned": pinned, "target": target, "distance": distance}


func restore(values: Dictionary) -> void:
	pinned = values.get("pinned", false)
	target = values.get("target", Vector3.ZERO)
	distance = values.get("distance", 600.0)
	set_enabled(values.get("enabled", false))


func _process(_delta: float) -> void:
	if not visible or not is_instance_valid(main_camera):
		return
	viewport.size = Vector2i(maxi(2, int(picture.size.x)), maxi(2, int(picture.size.y)))
	camera.keep_aspect = main_camera.keep_aspect
	camera.fov = main_camera.fov
	camera.near = main_camera.near
	camera.far = main_camera.far
	camera.cull_mask = main_camera.cull_mask
	# Parallel view directions preserve the shared W3D batches' back-to-front depth ordering.
	camera.global_transform = Transform3D(main_camera.global_basis, target + main_camera.global_basis.z * distance)
	var planes := camera.get_frustum()
	for node in instancers:
		if is_instance_valid(node):
			node.set_extra_pose_frustum(planes)


func _clear_frustum() -> void:
	for node in instancers:
		if is_instance_valid(node):
			node.set_extra_pose_frustum([])


func _exit_tree() -> void:
	_clear_frustum()

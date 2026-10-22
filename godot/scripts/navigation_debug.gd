## OpenBFME read-only diagnostics, enabled with --debug-navigation.
## Ground cell classification is not a unit-specific path legality test.
extends Node3D

var world: Node
var hud: Node
var paths_enabled := true
var terrain_enabled := true
var _covered := false
var _elapsed := 0.0
var _paths: MeshInstance3D
var _terrain: MeshInstance3D
var _ui: CanvasLayer
var _panel: VBoxContainer
var _status: Label


func _ready() -> void:
	name = "NavigationDebug"
	_paths = MeshInstance3D.new()
	_terrain = MeshInstance3D.new()
	for instance in [_paths, _terrain]:
		instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(instance)
	_ui = CanvasLayer.new()
	_ui.layer = 110
	add_child(_ui)
	_panel = VBoxContainer.new()
	_panel.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	_panel.position = Vector2(-380, 70)
	_panel.size.x = 370
	_ui.add_child(_panel)
	for item in [["Moving unit paths", "paths_enabled"], ["Ground navigation cells (600-unit area)", "terrain_enabled"]]:
		var check := CheckButton.new()
		check.text = item[0]
		check.button_pressed = get(item[1])
		check.toggled.connect(func(on: bool): set(item[1], on); refresh())
		_panel.add_child(check)
	_status = Label.new()
	_status.text = "NAV DEBUG • ground grid, not unit clearance\nGreen clear/rubble • amber pinched\nRed blocked • blue water\nCyan path • yellow direct steering/formation goal"
	_status.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_status.add_theme_color_override("font_outline_color", Color.BLACK)
	_status.add_theme_constant_override("outline_size", 4)
	_panel.add_child(_status)


func set_covered(covered: bool) -> void:
	_covered = covered
	visible = not covered
	_ui.visible = not covered


func _process(delta: float) -> void:
	if _covered or not is_instance_valid(world) or not is_instance_valid(hud): return
	_elapsed += delta
	if _elapsed < 0.2: return
	_elapsed = 0.0
	refresh()


func refresh() -> void:
	if not paths_enabled: _paths.mesh = null
	if not terrain_enabled: _terrain.mesh = null
	if not paths_enabled and not terrain_enabled: return
	if not is_instance_valid(world) or not is_instance_valid(hud): return
	var camera: Dictionary = hud.get_camera()
	if not camera.has("position"): return
	var center: Vector2 = camera.position
	var snapshot: Dictionary = world.debug_navigation(center.x, center.y, terrain_enabled)
	# The logic worker owns the state until its next client window. Keep the last
	# completed snapshot rather than reading live paths while they change.
	if not snapshot.get("ok", false): return
	apply_snapshot(snapshot)


func _material() -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	return mat


func apply_snapshot(snapshot: Dictionary) -> void:
	var lines := ImmediateMesh.new()
	var count := 0
	for route in snapshot.routes:
		count += maxi(0, route.points.size() - 1)
	if paths_enabled and count:
		lines.surface_begin(Mesh.PRIMITIVE_LINES, _material())
		for route in snapshot.routes:
			lines.surface_set_color(Color.CYAN if route.has_path else Color.YELLOW)
			for i in range(1, route.points.size()):
				lines.surface_add_vertex(route.points[i - 1])
				lines.surface_add_vertex(route.points[i])
		lines.surface_end()
	_paths.mesh = lines if paths_enabled and count else null
	if not terrain_enabled: return
	var vertices: PackedVector3Array = snapshot.vertices
	var width: int = snapshot.width
	var height: int = snapshot.height
	var grid := ImmediateMesh.new()
	if width and height:
		grid.surface_begin(Mesh.PRIMITIVE_TRIANGLES, _material())
		for y in height:
			for x in width:
				var cell := y * width + x
				var type: int = snapshot.types[cell]
				var color := Color(0.9, 0.12, 0.08, 0.3)
				if type in [0, 3]: color = Color(0.1, 0.9, 0.2, 0.24)
				if type in [1, 7]: color = Color(0.1, 0.4, 1.0, 0.3)
				if snapshot.pinched[cell]: color = Color(1.0, 0.65, 0.05, 0.4)
				grid.surface_set_color(color)
				var a := y * (width + 1) + x
				for index in [a, a + 1, a + width + 1, a + 1, a + width + 2, a + width + 1]:
					grid.surface_add_vertex(vertices[index])
		grid.surface_end()
	_terrain.mesh = grid if width and height else null

## Two input surfaces for one game/player, with independent camera positions.
extends CanvasLayer

var source: Camera3D
var game: Node
var button: Button
var window: Window
var camera: Camera3D
var world: Node
var restore_culling := true
var hud: Node
var primary_hud: Node
var secondary_active := false
var _view_options := {}


func _ready() -> void:
	layer = 110
	button = Button.new()
	button.text = "Pop out view"
	button.tooltip_text = "Open another game view with minimap, Palantir and normal game controls."
	button.focus_mode = Control.FOCUS_NONE
	button.position = Vector2(16, 48)
	button.pressed.connect(open_view)
	add_child(button)
	process_priority = 110
	get_window().focus_entered.connect(func(): _activate(false))


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
		elif primary_hud != game._hud:
			close_view()
		else:
			_sync_options()
			var blocked := _input_blocked()
			hud.set_process_input(not blocked)
			if blocked:
				_activate(false)
				if window.has_focus():
					get_window().grab_focus()
			elif window.has_focus():
				_activate(true)


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
	window.focus_entered.connect(func(): _activate(true))
	window.focus_exited.connect(func(): _deactivate())
	add_child(window)
	camera = source.duplicate(0) as Camera3D
	window.add_child(camera)
	camera.global_transform = source.global_transform
	camera.global_position += source.global_basis.x * 400.0
	camera.make_current()
	primary_hud = game._hud
	hud = _create_hud()
	window.add_child(hud)
	var state: Dictionary = primary_hud.get_camera()
	var target: Vector2 = state.position
	var options := {"camera_start": Vector3(target.x + 400.0, 0, -target.y),
		"secondary_view": true, "alternate_mouse": game._alternate_mouse_setup(),
		"free_camera": state.free_camera, "zoom_out_factor": state.zoom_out_factor,
		"widescreen_wider": state.widescreen_wider}
	var result: Dictionary = hud.setup(game._fs, world, camera, game._local_name, options)
	if not result.ok:
		push_error("Second view HUD: " + str(result.errors))
		close_view()
		return
	hud.camera_set_height(state.height_above_ground)
	hud.camera_set_angle_pitch(state.angle, state.pitch)
	hud.set_view_active(false)
	hud.link_secondary_view(primary_hud)
	_sync_options()
	window.show()
	_activate(true)


func _create_hud() -> Node:
	return ClassDB.instantiate("InGameHudNode")


func _sync_options() -> void:
	if game._game_options == null:
		return
	var options: Dictionary = game._game_options.view_options(game._game_options.current())
	if options != _view_options:
		_view_options = options
		hud.set_view_options(options)


func _activate(secondary: bool) -> void:
	if not is_instance_valid(hud) or not is_instance_valid(primary_hud):
		return
	secondary = secondary and not _input_blocked()
	secondary_active = secondary
	# Restore the destination's own selection before accepting its input.
	if secondary:
		hud.set_view_active(true)
		primary_hud.set_view_active(false)
	else:
		primary_hud.set_view_active(true)
		hud.set_view_active(false)


func _input_blocked() -> bool:
	return game._quit_open or (game._lua_console != null and game._lua_console.is_open()) or game._shell.menu_has_input()


func _deactivate() -> void:
	if is_instance_valid(hud):
		hud.set_view_active(false)


func _view_input(event: InputEvent) -> void:
	# Native HUD input is scoped to this Window: no coordinate forwarding or
	# duplicate keyboard injection into the main HUD. Escape's game menu is global.
	if game._lua_console != null and game._lua_console.is_toggle_event(event):
		game._lua_console.toggle()
		get_window().grab_focus()
		_activate(false)
		window.set_input_as_handled()
		return
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_ESCAPE:
		if hud.get_spellbook_state().get("store", {}).get("open", false):
			hud.close_spell_store()
		else:
			game._input(event)
			get_window().grab_focus()
			_activate(false)
		window.set_input_as_handled()


func close_view() -> void:
	_activate(false)
	if is_instance_valid(hud):
		# Detach hooks and world-backed drawables before a game/world teardown.
		hud.free()
	hud = null
	primary_hud = null
	secondary_active = false
	_view_options.clear()
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

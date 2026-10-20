extends SceneTree

class FakeWorld extends Node:
	var culling := true
	func get_perf3_client() -> bool: return culling
	func set_perf3_client(value: bool) -> void: culling = value

class FakeGame extends Node:
	enum State { PLAYING, MENU }
	var _state := State.PLAYING
	var _hud: Node
	var _world: Node
	var _quit_open := false
	var _lua_console: Node
	var _shell: Node = FakeShell.new()
	var _fs: RefCounted
	var _local_name := "Player_1"
	var _game_options: Node
	func _alternate_mouse_setup() -> bool: return true
	func _exit_tree() -> void: _shell.free()

class FakeShell extends Node:
	func menu_has_input() -> bool: return false

class FakeHud extends Node:
	var active := true
	var options := {}
	var peer: Node
	func get_camera() -> Dictionary:
		return {"position": Vector2.ZERO, "height_above_ground": 300.0,
			"angle": 0.0, "pitch": 0.0, "free_camera": false,
			"zoom_out_factor": 1.0, "widescreen_wider": false}
	func setup(_fs, _world, _camera, _local, values: Dictionary) -> Dictionary:
		options = values
		return {"ok": true}
	func camera_set_height(_height: float) -> void: pass
	func camera_set_angle_pitch(_angle: float, _pitch: float) -> void: pass
	func link_secondary_view(primary: Node) -> void: peer = primary
	func set_view_active(value: bool) -> void: active = value

class TestView extends "res://scripts/second_view.gd":
	func _create_hud() -> Node: return FakeHud.new()

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	assert(load("res://scripts/game.gd").can_instantiate())
	var game := FakeGame.new()
	root.add_child(game)
	game._hud = FakeHud.new()
	game.add_child(game._hud)
	game._world = FakeWorld.new()
	game.add_child(game._world)
	var source := Camera3D.new()
	game.add_child(source)
	source.position = Vector3(0, 500, 600)
	source.make_current()
	var view = TestView.new()
	view.game = game
	view.source = source
	game.add_child(view)
	view.open_view()
	assert(view.window.world_3d == source.get_world_3d())
	assert(view.window.force_native)
	assert(view.window.get_child_count() == 2)
	assert(view.hud.peer == game._hud)
	assert(view.hud.options.secondary_view)
	assert(view.hud.options.alternate_mouse)
	assert(view.hud.active and not game._hud.active)
	assert(view.camera.global_position == Vector3(400, 500, 600))
	assert(root.get_camera_3d() == source)
	assert(game._world.culling == false)
	view._activate(false)
	assert(game._hud.active and not view.hud.active)
	view._activate(true)
	view._deactivate()
	assert(not view.hud.active)
	view._activate(true)
	assert(view.hud.active and not game._hud.active)
	assert(source.position == Vector3(0, 500, 600))
	view.close_view()
	assert(view.window == null)
	assert(game._hud.active)
	assert(game._world.culling == true)
	await process_frame
	view.open_view()
	game._state = FakeGame.State.MENU
	view._process(0)
	assert(view.window == null)
	assert(game._world.culling == true)
	game._state = FakeGame.State.PLAYING
	view.open_view()
	var previous_world: Node = game._world
	game._world = FakeWorld.new()
	game.add_child(game._world)
	view._process(0)
	assert(view.window == null)
	assert(previous_world.culling == true)
	view.open_view()
	view.queue_free()
	await process_frame
	assert(game._world.culling == true)
	game.queue_free()
	await process_frame
	print("second_view_test: PASS")
	quit()

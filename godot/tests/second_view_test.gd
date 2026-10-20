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

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	assert(load("res://scripts/game.gd").can_instantiate())
	var game := FakeGame.new()
	root.add_child(game)
	game._hud = Node.new()
	game.add_child(game._hud)
	game._world = FakeWorld.new()
	game.add_child(game._world)
	var source := Camera3D.new()
	game.add_child(source)
	source.position = Vector3(0, 500, 600)
	source.make_current()
	var view = preload("res://scripts/second_view.gd").new()
	view.game = game
	view.source = source
	game.add_child(view)
	view.open_view()
	assert(view.window.world_3d == source.get_world_3d())
	assert(view.window.force_native)
	assert(view.window.get_child_count() == 1)
	assert(view.camera.global_position == Vector3(400, 500, 600))
	assert(root.get_camera_3d() == source)
	assert(game._world.culling == false)
	var zoom := InputEventMouseButton.new()
	zoom.button_index = MOUSE_BUTTON_WHEEL_UP
	zoom.pressed = true
	view._view_input(zoom)
	assert(view.camera.global_position == Vector3(400, 500, 540))
	assert(source.position == Vector3(0, 500, 600))
	view.close_view()
	assert(view.window == null)
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

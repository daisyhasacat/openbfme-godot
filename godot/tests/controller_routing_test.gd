extends SceneTree
class Shell extends Node:
	var active := false
	func menu_has_input() -> bool: return active
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var shell := Shell.new(); root.add_child(shell)
	var controller = load("res://scripts/controller_input.gd").new()
	controller.gameplay = func(): return not shell.active
	controller.area_selection = func(radius: float, _commit: bool): return radius == 0.0
	var generated: Array = []
	var pans: Array = []
	controller.input_generated.connect(func(event): generated.append(event))
	controller.camera_pan.connect(func(amount): pans.append(amount))
	root.add_child(controller); controller.set_process(false)
	# Same reverse input order as the actual game: MenuPad is added last.
	var pad = load("res://scripts/menu_pad.gd").new(shell)
	root.add_child(pad); pad.set_process(false)
	var trigger := InputEventJoypadMotion.new()
	trigger.axis = JOY_AXIS_TRIGGER_RIGHT; trigger.axis_value = 1.0
	root.push_input(trigger)
	assert(controller.palette_open)
	trigger.axis_value = 0.0; root.push_input(trigger)
	assert(not controller.palette_open)
	var a := InputEventJoypadButton.new(); a.button_index = JOY_BUTTON_A; a.pressed = true
	root.push_input(a)
	assert(controller._area_pending)
	a.pressed = false; root.push_input(a)
	assert(not controller._area_pending)
	var clicks := generated.filter(func(event): return event is InputEventMouseButton)
	assert(clicks.size() == 2 and clicks[0].pressed and not clicks[1].pressed)
	controller._pan_stick(Vector2.RIGHT, 0.1)
	assert(pans == [Vector2(120, 0)])
	assert(pad.take_log().is_empty())
	# A menu-owned press may start a game: its release must stay out of gameplay.
	shell.active = true; a.pressed = true; root.push_input(a)
	assert(pad._captured_buttons.has(JOY_BUTTON_A))
	shell.active = false; pad._process(0.1)
	a.pressed = false; root.push_input(a)
	assert(pad._captured_buttons.is_empty() and not controller._area_pending)
	pad.free(); controller.free(); shell.free()
	print("CONTROLLER ROUTING PASS")
	quit()

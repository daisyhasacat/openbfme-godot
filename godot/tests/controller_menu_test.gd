extends SceneTree

class FakeShell extends Node2D:
	var stack := PackedStringArray(["MainMenu.apt"])
	var calls: Array = []
	var sections: Array = []
	var active := true
	func menu_controller_section(delta: int) -> bool:
		sections.append(delta)
		return true
	func shell_stack() -> PackedStringArray: return stack
	func menu_has_input() -> bool: return active
	func shell_fscommand(command: String, argument: String) -> bool:
		calls.append([command, argument])
		return true

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var game = load("res://scripts/game.gd").new()
	var shell := FakeShell.new()
	game._shell = shell
	game._state = game.State.MENU
	game._controller_menu_action("skirmish")
	assert(shell.calls == [["AptMainMenu::Skirmish", ""]])
	shell.stack = PackedStringArray(["MainMenu.apt", "Skirmish.apt"])
	game._controller_menu_action("skirmish")
	assert(shell.calls.back() == ["AptSkirmish::StartGame", ""])
	# The real Start-button routing starts this lobby instead of generating Esc.
	var pad = load("res://scripts/menu_pad.gd").new(shell)
	root.add_child(pad)
	pad.set_process(false)
	pad.start_lobby = game._controller_start_lobby
	var start := InputEventJoypadButton.new()
	start.button_index = JOY_BUTTON_START
	start.pressed = true
	pad._input(start)
	start.pressed = false
	pad._input(start)
	assert(shell.calls.size() == 3)
	assert(shell.calls.back() == ["AptSkirmish::StartGame", ""])
	assert(pad.take_log().is_empty(), "Start must not also generate Escape")
	shell.stack.append("Options.apt")
	game._controller_menu_action("skirmish")
	game._controller_menu_action("jumpToSelection")
	assert(shell.calls.size() == 3)
	shell.stack = PackedStringArray(["MainMenu.apt"])
	game._state = game.State.PLAYING
	game._controller_menu_action("skirmish")
	assert(shell.calls.size() == 3)
	var trigger := InputEventJoypadMotion.new()
	trigger.axis = JOY_AXIS_TRIGGER_LEFT
	for value in [0.8, 0.9, 0.0, 0.8, 0.0]:
		trigger.axis_value = value
		pad._input(trigger)
	trigger.axis = JOY_AXIS_TRIGGER_RIGHT
	trigger.axis_value = 0.8
	pad._input(trigger)
	trigger.axis_value = 0.0
	pad._input(trigger)
	assert(shell.sections == [1, 1, -1], "Triggers switch once per pull")
	assert(pad.take_log().is_empty(), "Section triggers must not also send arrow keys")

	var events: Array = []
	pad.input_generated.connect(func(event: InputEvent) -> void: events.append(event))
	var stick := InputEventJoypadMotion.new()
	stick.axis = JOY_AXIS_LEFT_X
	stick.axis_value = 0.2
	pad._input(stick)
	pad._process(0.01)
	assert(not pad.mouse_mode and events.is_empty(), "Left-stick drift must not enable mouse mode")
	stick.axis_value = 1.0
	pad._input(stick)
	pad._process(0.01)
	assert(pad.mouse_mode and events.size() == 1 and events[0] is InputEventMouseMotion)
	assert(pad.take_log().is_empty(), "Left stick moves the mouse without arrow keys")
	var a := InputEventJoypadButton.new()
	a.button_index = JOY_BUTTON_A
	a.pressed = true
	pad._input(a)
	a.pressed = false
	pad._input(a)
	assert(events.size() == 3)
	assert(events[1] is InputEventMouseButton and events[1].pressed)
	assert(events[2] is InputEventMouseButton and not events[2].pressed)
	assert(pad.take_log().is_empty(), "Mouse-mode A must not also activate the menu")
	# A drag ends safely when navigation takes over, and A's release stays consumed.
	a.pressed = true
	pad._input(a)
	var direction := InputEventJoypadButton.new()
	direction.button_index = JOY_BUTTON_DPAD_DOWN
	direction.pressed = true
	pad._input(direction)
	assert(not pad.mouse_mode and not pad._mouse_down)
	a.pressed = false
	pad._input(a)
	direction.pressed = false
	pad._input(direction)
	assert(pad.take_log() == PackedStringArray(["down Down", "up Down"]))
	events.clear()
	pad._process(0.01)
	assert(not pad.mouse_mode and events.is_empty(), "Held left stick must be centered before returning to mouse mode")
	a.pressed = true
	pad._input(a)
	a.pressed = false
	pad._input(a)
	assert(events.size() == 2 and events[0] is InputEventKey and events[1] is InputEventKey)
	assert(pad.take_log() == PackedStringArray(["down Enter", "up Enter"]))
	# Center/reuse left stick, then switch back using the right stick's vertical axis.
	stick.axis_value = 0.0
	pad._input(stick)
	stick.axis_value = 1.0
	pad._input(stick)
	pad._process(0.01)
	assert(pad.mouse_mode)
	stick.axis = JOY_AXIS_RIGHT_Y
	stick.axis_value = -1.0
	pad._input(stick)
	assert(not pad.mouse_mode)
	stick.axis_value = 0.0
	pad._input(stick)
	assert(pad.take_log() == PackedStringArray(["down Up", "up Up"]))
	stick.axis = JOY_AXIS_RIGHT_X
	stick.axis_value = 1.0
	pad._input(stick)
	pad._held[KEY_RIGHT] = 0.0
	pad._process(0.01)
	stick.axis_value = 0.0
	pad._input(stick)
	assert(pad.take_log() == PackedStringArray(["down Right", "down Right (repeat)", "up Right"]))
	# Focus loss releases nonrepeating accept/back/tab keys as well as directions.
	for index in [JOY_BUTTON_A, JOY_BUTTON_B, JOY_BUTTON_LEFT_SHOULDER]:
		var held_button := InputEventJoypadButton.new()
		held_button.button_index = index
		held_button.pressed = true
		pad._input(held_button)
	pad._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	assert(pad._pressed_keys.is_empty() and pad._held.is_empty())
	assert(pad.take_log() == PackedStringArray(["down Enter", "down Escape", "down PageUp", "up Enter", "up Escape", "up PageUp"]))
	pad._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	# Closing a menu and disconnecting release a mouse press and clear the mode.
	pad._pointer_stick = Vector2.ZERO
	pad._wait_pointer_neutral = false
	pad._move_pointer(Vector2.RIGHT, 0.01)
	a.pressed = true
	pad._input(a)
	shell.active = false
	pad._process(0.01)
	assert(not pad.mouse_mode and not pad._mouse_down)
	shell.active = true
	pad._move_pointer(Vector2.RIGHT, 0.01)
	pad._input(a)
	pad._connection(0, false)
	assert(not pad.mouse_mode and not pad._mouse_down and pad._device == -1)
	pad._input(a)
	assert(pad._device == -1, "Queued events must not revive a disconnected pad")
	pad.queue_free()
	game._shell = null
	game.free()
	shell.free()
	print("CONTROLLER MENU PASS")
	quit()

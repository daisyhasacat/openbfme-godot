extends SceneTree

class FakeShell extends Node2D:
	var stack := PackedStringArray(["MainMenu.apt"])
	var calls: Array = []
	func shell_stack() -> PackedStringArray: return stack
	func menu_has_input() -> bool: return true
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
	game._shell = null
	game.free()
	shell.free()
	print("CONTROLLER MENU PASS")
	quit()

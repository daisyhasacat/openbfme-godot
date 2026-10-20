## A failed save must return to an automatically ticking shell, with its load screen gone.
extends SceneTree

class TestShell extends RefCounted:
	var auto_process := false
	var still_visible := true
	var refused_name := ""
	var refused_error := ""
	var pushed := ""
	func hide_still_load_screen() -> void: still_visible = false
	func set_still_load_progress(_percent: int, _text: String) -> void: pass
	func set_save_load(_info: Dictionary) -> void: pass
	func shell_push(movie: String) -> bool:
		pushed = movie
		return true
	func save_load_refused(name: String, error: String) -> void:
		refused_name = name
		refused_error = error

class TestWorld extends RefCounted:
	func list_saves(_directory: String) -> Array: return []

class TestGame extends Node:
	enum State {MENU, SCORE}
	var _state := State.MENU
	var _save_test := ""
	var _start_stack := PackedStringArray()
	var _save_dir_arg := OS.get_environment("TMPDIR").path_join("openbfme-save-error-test")
	var _shell := TestShell.new()
	var _world := TestWorld.new()
	var continued := false
	func _score_continue() -> void:
		continued = true
		_state = State.MENU

func _initialize() -> void:
	var game := TestGame.new()
	root.add_child(game)
	var flow = load("res://scripts/save_flow.gd").new(game)
	flow._overlay_on = true
	flow._back_to_menu("Broken Save", "snapshot refused")
	assert(game.continued)
	assert(game._state == TestGame.State.MENU)
	assert(game._shell.auto_process)
	assert(not game._shell.still_visible)
	assert(not flow._overlay_on)
	assert(game._shell.pushed == "SaveLoad.apt")
	assert(game._shell.refused_name == "Broken Save")
	assert(game._shell.refused_error == "snapshot refused")
	DirAccess.remove_absolute(game._save_dir_arg)
	print("SAVE LOAD ERROR PASS")
	quit(0)

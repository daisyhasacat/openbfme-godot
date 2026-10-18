extends SceneTree

class ShellStub extends Node2D:
	var auto_process := false

const RECOVERY_SOURCE = """
extends "res://scripts/game.gd"
var calls: Array[String] = []
var shown_error := ""
var restoration_fails := false

func _hide_rules_summary() -> void:
	calls.append("hide summary")

func _tear_down_game(why: String) -> void:
	calls.append(why)

func _restore_start_screens() -> void:
	assert(_shell.auto_process)
	calls.append("restore menu")
	_state = State.FAILED if restoration_fails else State.MENU

func _show_load_error(message: String) -> void:
	calls.append("show error")
	shown_error = message

"""

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var script := GDScript.new()
	script.source_code = RECOVERY_SOURCE
	assert(script.reload() == OK)
	var game = script.new()
	var shell := ShellStub.new()
	game._shell = shell
	game._state = game.State.LOADING
	var error := "script library Libraries\\Lib_GollumSpawn\\Lib_GollumSpawn.map: file not found"
	game._recover_local_load_error({"ok": false, "errors": [error]})
	assert(game.calls == ["hide summary", "load failed", "restore menu", "show error"])
	assert(game._state == game.State.MENU)
	assert(game.shown_error.contains(error))
	game.calls.clear()
	game.shown_error = ""
	game.restoration_fails = true
	game._recover_local_load_error({"ok": false, "errors": [error]})
	assert(game.calls == ["hide summary", "load failed", "restore menu"])
	assert(game.shown_error.is_empty())
	game.free()
	shell.free()
	print("LOAD FAILURE RECOVERY PASS")
	quit()

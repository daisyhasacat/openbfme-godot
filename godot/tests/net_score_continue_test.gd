## OpenBFME. GPL-3.0.
## Score Continue restores UI lobbies without opening an empty direct-network address.
## godot --headless --path godot --script res://tests/net_score_continue_test.gd
extends SceneTree


class ShellStub:
	extends Node2D
	var stack := PackedStringArray(["TimeLine.apt"])
	func shell_stack() -> PackedStringArray:
		return stack
	func shell_pop() -> void:
		stack.resize(stack.size() - 1)
	func tick(_delta: float) -> void:
		pass
	func shell_push(screen: String) -> bool:
		stack.append(screen)
		return true
	func shell_top_level() -> int:
		return stack.size()
	func has_level(level: int) -> bool:
		return level > 0 and level <= stack.size()
	func instance_info(level: int, _path: String) -> Dictionary:
		return {"visible": level == stack.size()}


class AudioStub:
	extends Node
	func stop_music(_fade: bool) -> void:
		pass
	func play_shell_music(_fade: bool) -> void:
		pass


const GAME_PROBE_SOURCE := """
extends "res://scripts/game.gd"
var opens := 0
var failures: Array[String] = []
func _net_open() -> void:
	opens += 1
	_net_state = "lobby"
func _shell_back_from_game() -> void:
	pass
func _fail(message: String) -> void:
	failures.append(message)
"""


var _failures := 0


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _initialize() -> void:
	var probe_script := GDScript.new()
	probe_script.source_code = GAME_PROBE_SOURCE
	var compile_error := probe_script.reload()
	_check(compile_error == OK, "GameProbe subclass compiles")
	if compile_error != OK:
		quit(1)
		return
	for spec in [
		{"name": "UI LAN", "network": true, "host": -1, "join": "", "screens": ["MainMenu.apt", "LanLobby.apt"], "opens": 0},
		{"name": "UI online", "network": true, "host": -1, "join": "", "screens": ["MainMenu.apt"], "opens": 0},
		{"name": "direct host", "network": true, "host": 31000, "join": "", "screens": ["MainMenu.apt"], "opens": 1},
		{"name": "direct join", "network": true, "host": -1, "join": "127.0.0.1:31000", "screens": ["MainMenu.apt"], "opens": 1},
		{"name": "non-network", "network": false, "host": -1, "join": "", "screens": ["MainMenu.apt", "Skirmish.apt"], "opens": 0},
	]:
		var game = probe_script.new()
		var shell := ShellStub.new()
		var audio := AudioStub.new()
		game._shell = shell
		game._audio = audio
		game._state = game.State.SCORE
		game._start_stack = PackedStringArray(spec.screens)
		game._net_lobby_again = spec.network
		game._net_host = spec.host
		game._net_join = spec.join
		game._score_continue()
		_check(game.failures.is_empty(), "%s: no fatal transition" % spec.name)
		_check(shell.stack == PackedStringArray(spec.screens), "%s: restored screens" % spec.name)
		_check(game._state == game.State.MENU, "%s: returned to menu" % spec.name)
		_check(not game._net_lobby_again, "%s: consumed return flag" % spec.name)
		_check(game.opens == spec.opens, "%s: direct-session open count" % spec.name)
		game._score_continue()
		_check(game.opens == spec.opens, "%s: duplicate Continue is ignored" % spec.name)
		game.free()
		shell.free()
		audio.free()
	print("NET SCORE CONTINUE ", "PASS" if _failures == 0 else "FAIL")
	quit(0 if _failures == 0 else 1)

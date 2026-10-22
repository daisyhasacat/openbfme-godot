extends SceneTree
const Diagnostic :=  preload("res://scripts/local_diagnostic_control.gd")
class FakeShell extends Node:
	func shell_stack() -> PackedStringArray: return PackedStringArray(["MainMenu.apt"])
class FakeGame extends Node:
	enum State {BOOT, MENU, PLAYING, SCORE}
	var _state := State.MENU
	var _benchmark_tool = null
	var _shell = FakeShell.new()
	var _quit_open := false
	var _hud = null
	func _exit_tree() -> void: _shell.free()

var _logger = preload("res://scripts/walk_error_logger.gd").new()
func _initialize() -> void:
	OS.add_logger(_logger)
	process_frame.connect(func():
		if _logger.script_errors() > 0: quit(1))
	create_timer(30.0).timeout.connect(func(): quit(1))
	call_deferred("_run")
func _run() -> void:
	var game := FakeGame.new()
	root.add_child(game)
	var control := Diagnostic.new()
	control.game = game
	control.directory = "/tmp/openbfme-local-control-test-" + str(Time.get_ticks_usec())
	root.add_child(control)
	assert(not Diagnostic._valid_id("../escape"))
	assert(Diagnostic._valid_id("safe-id_123"))
	assert(control.execute({"version": 1, "id": "status-1", "command": "status"}).state.state == "MENU")
	assert(control.execute({"version": 1, "id": "status-2", "command": "status"}).status == "ok")
	assert(control.execute({"version": 1, "id": "status-1", "command": "status"}).status == "duplicate", "Intervening requests must not allow replay")
	assert(control.execute({"version": 2, "id": "bad-version", "command": "status"}).status == "error")
	assert(control.execute({"version": 1, "id": "bad-command", "command": "execute_lua"}).status == "error")
	assert(control.execute({"version": 1, "id": "bad-duration", "command": "benchmark_start", "seconds": "3"}).status == "error")
	assert(control.execute({"version": 1, "id": "bad-camera", "command": "camera"}).status == "error")
	# Exercise the file protocol rather than only the dispatcher.
	var file := FileAccess.open(control.directory.path_join("request.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify({"version": 1, "id": "from-file", "command": "status"}))
	file.close()
	control._next_poll = 0
	control._process(0)
	var reply = JSON.parse_string(FileAccess.get_file_as_string(control.directory.path_join("response.json")))
	assert(reply.id == "from-file" and reply.status == "ok")
	assert(not FileAccess.file_exists(control.directory.path_join("request.json")))
	# Incomplete transfers are left intact; a completed replacement is accepted.
	file = FileAccess.open(control.directory.path_join("request.json"), FileAccess.WRITE)
	file.store_string("{\"version\":1,")
	file.close()
	control._next_poll = 0
	control._process(0)
	assert(FileAccess.file_exists(control.directory.path_join("request.json")))
	file = FileAccess.open(control.directory.path_join("request.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify({"version": 1, "id": "after-partial", "command": "status"}))
	file.close()
	control._next_poll = 0
	control._process(0)
	reply = JSON.parse_string(FileAccess.get_file_as_string(control.directory.path_join("response.json")))
	assert(reply.id == "after-partial" and reply.status == "ok")
	file = FileAccess.open(control.directory.path_join("request.json"), FileAccess.WRITE)
	file.store_string("x".repeat(32769))
	file.close()
	control._next_poll = 0
	control._process(0)
	reply = JSON.parse_string(FileAccess.get_file_as_string(control.directory.path_join("response.json")))
	assert(reply.status == "error" and not FileAccess.file_exists(control.directory.path_join("request.json")))
	# A new controller instance still suppresses previously consumed commands.
	file = FileAccess.open(control.directory.path_join("job.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify({"id": "interrupted", "status": "running"}))
	file.close()
	var again := Diagnostic.new()
	again.game = game
	again.directory = control.directory
	root.add_child(again)
	assert(JSON.parse_string(FileAccess.get_file_as_string(control.directory.path_join("job.json"))).status == "failed")
	assert(again.execute({"version": 1, "id": "from-file", "command": "status"}).status == "duplicate")
	var directory: String = control.directory
	again.free()
	control.free()
	for name in DirAccess.get_files_at(directory.path_join("receipts")):
		DirAccess.remove_absolute(directory.path_join("receipts").path_join(name))
	DirAccess.remove_absolute(directory.path_join("receipts"))
	for name in DirAccess.get_files_at(directory): DirAccess.remove_absolute(directory.path_join(name))
	DirAccess.remove_absolute(directory)
	game.free()
	print("LOCAL CONTROL PASS")
	quit()

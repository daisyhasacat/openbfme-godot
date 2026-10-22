extends SceneTree
func _initialize() -> void:
	call_deferred("run")
func run() -> void:
	var controller = load("res://scripts/remote_testing.gd").new()
	# Server commands are restricted and a repeated run cannot restart its scenario.
	controller._start({"action": "shell", "run": "invalid"})
	assert(controller.command.is_empty())
	controller.last_run = "0123456789abcdef"
	controller.command = {"role": "join"}
	controller.status = {"run": controller.last_run, "state": "failed"}
	controller.token = "test-token"
	controller._done(HTTPRequest.RESULT_SUCCESS, 200, PackedStringArray(), JSON.stringify({"v": 1, "command": {"action": "run", "run": controller.last_run}}).to_utf8_buffer())
	assert(controller.status.state == "failed")
	controller._done(HTTPRequest.RESULT_CANT_CONNECT, 0, PackedStringArray(), PackedByteArray())
	assert(controller.transport_errors == 1)
	assert(controller.last_run == "0123456789abcdef")
	controller.free()
	print("REMOTE TEST CONTROL PASS: invalid commands, repeated runs, transport failure")
	quit()

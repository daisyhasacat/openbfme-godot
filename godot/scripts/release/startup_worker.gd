## Exclusive startup work. The callable must not touch nodes or rendering/audio devices.
## Its inputs remain owned and unused by the main thread until the worker joins.
extends Node

var _thread: Thread
var _result: Dictionary = {}

func run(work: Callable) -> Dictionary:
	if _thread != null:
		return {"ok":false,"errors":["Startup worker is already in use"]}
	_thread = Thread.new()
	var error := _thread.start(work)
	if error != OK:
		_thread = null
		return {"ok":false,"errors":["Could not start startup worker: " + error_string(error)]}
	while _thread.is_alive():
		await get_tree().process_frame
	if _thread.is_started():
		_result = _thread.wait_to_finish()
	return _result

func _exit_tree() -> void:
	# Quit/restart must not release archives or callbacks while their worker uses them.
	if _thread != null and _thread.is_started():
		_result = _thread.wait_to_finish()

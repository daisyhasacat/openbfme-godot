extends SceneTree

const InstanceLock := preload("res://scripts/core/instance_lock.gd")
const GameStart := preload("res://scripts/core/game_start.gd")

class ScriptedLock:
	extends "res://scripts/core/instance_lock.gd"

	var query_result: Dictionary

	func _init(data_dir: String, version: String, result: Dictionary) -> void:
		super(data_dir, version)
		query_result = result

	func _windows_exclusions() -> Dictionary:
		return query_result


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	match _value(args, "--mode=", ""):
		"profile":
			_profile()
		"try":
			_try_lock(args, false)
		"hold":
			_try_lock(args, true)
		"background":
			_background_holder(args)
		"release-reacquire":
			_release_reacquire()
		"takeover":
			_takeover()
		_:
			_emit({"event": "error", "error": "unknown or missing --mode"})
			quit(2)


func _profile() -> void:
	var data_dir := OS.get_user_data_dir().simplify_path()
	var expected := OS.get_environment("OPENBFME_EXPECTED_USER_DATA").replace("\\", "/").simplify_path()
	var lock := InstanceLock.new(data_dir, "profile")
	var matches := expected == "" or data_dir.to_lower() == expected.to_lower()
	_emit({
		"event": "profile",
		"user_data_dir": data_dir,
		"base_port": lock.port,
		"godot_version": Engine.get_version_info().get("string", ""),
		"expected_match": matches
	})
	quit(0 if matches else 1)


func _try_lock(args: PackedStringArray, hold: bool) -> void:
	var data_dir := _value(args, "--data-dir=", OS.get_user_data_dir())
	var version := _value(args, "--version=", "native-test")
	var lock: InstanceLock
	var query_result := _query_result(args)
	if query_result.is_empty():
		lock = InstanceLock.new(data_dir, version)
	else:
		lock = ScriptedLock.new(data_dir, version, query_result)
	var first_port := _value(args, "--first-port=", "")
	if not first_port.is_empty():
		lock._first_port = int(first_port)
		lock.port = int(first_port)
	var acquired := lock.acquire()
	_emit({
		"event": "lock",
		"mode": "hold" if hold else "try",
		"acquired": acquired,
		"version": version,
		"port": lock.port,
		"error": lock.error,
		"other_launcher": lock.other_launcher,
		"godot_version": Engine.get_version_info().get("string", ""),
		"user_data_dir": OS.get_user_data_dir().simplify_path()
	})
	if not acquired or not hold:
		if acquired:
			lock.release()
		quit(0)
		return
	var release_file := _value(args, "--release-file=", "")
	var limit := int(_value(args, "--hold-ms=", "120000"))
	var deadline := Time.get_ticks_msec() + limit
	while Time.get_ticks_msec() < deadline and (release_file == "" or not FileAccess.file_exists(release_file)):
		OS.delay_msec(25)
	lock.release()
	_emit({"event": "released", "port": lock.port})
	quit(0)


func _background_holder(args: PackedStringArray) -> void:
	var version := _value(args, "--version=", "native background") + " background"
	var lock := InstanceLock.new(OS.get_user_data_dir(), version)
	var acquired := lock.acquire()
	_emit({
		"event": "lock",
		"mode": "background",
		"acquired": acquired,
		"version": version,
		"port": lock.port,
		"error": lock.error,
		"other_launcher": lock.other_launcher,
		"godot_version": Engine.get_version_info().get("string", "")
	})
	if not acquired:
		quit(0)
		return
	var stop := OS.get_user_data_dir().path_join("game-start.stop")
	var deadline := Time.get_ticks_msec() + 30000
	while Time.get_ticks_msec() < deadline and not FileAccess.file_exists(stop):
		OS.delay_msec(25)
	lock.release()
	_emit({"event": "background_released", "port": lock.port, "stop_file_seen": FileAccess.file_exists(stop)})
	quit(0)


func _release_reacquire() -> void:
	var first := InstanceLock.new(OS.get_user_data_dir(), "release-reacquire")
	var acquired_first := first.acquire()
	var hello := _probe_hello(first.port) if acquired_first else ""
	first.release()
	var second := InstanceLock.new(OS.get_user_data_dir(), "release-reacquire")
	var acquired_second := second.acquire()
	_emit({
		"event": "release_reacquire",
		"first_acquired": acquired_first,
		"probe_hello": hello,
		"second_acquired": acquired_second,
		"port": second.port,
		"error": second.error
	})
	second.release()
	quit(0 if acquired_first and acquired_second and hello.begins_with("OPENBFME-LAUNCHER release-reacquire") else 1)


func _probe_hello(candidate: int) -> String:
	var peer := StreamPeerTCP.new()
	if peer.connect_to_host("127.0.0.1", candidate) != OK:
		return ""
	var got := PackedByteArray()
	var start := Time.get_ticks_msec()
	while Time.get_ticks_msec() - start < 3000 and not got.has(10):
		peer.poll()
		if peer.get_status() == StreamPeerTCP.STATUS_CONNECTED and peer.get_available_bytes() > 0:
			var result := peer.get_partial_data(mini(peer.get_available_bytes(), 256))
			if result[0] == OK:
				got.append_array(result[1])
		elif peer.get_status() == StreamPeerTCP.STATUS_ERROR or peer.get_status() == StreamPeerTCP.STATUS_NONE:
			break
		OS.delay_msec(20)
	peer.disconnect_from_host()
	return got.get_string_from_utf8().get_slice("\n", 0)


func _takeover() -> void:
	var lock := InstanceLock.new(OS.get_user_data_dir(), "foreground")
	var initially_acquired := lock.acquire()
	var observed := lock.other_launcher
	var taken_over := not initially_acquired and GameStart.take_over(lock)
	_emit({
		"event": "takeover",
		"initially_acquired": initially_acquired,
		"observed_launcher": observed,
		"taken_over": taken_over,
		"port": lock.port,
		"error": lock.error,
		"godot_version": Engine.get_version_info().get("string", "")
	})
	lock.release()
	quit(0 if not initially_acquired and observed.ends_with(" background") and taken_over else 1)


func _query_result(args: PackedStringArray) -> Dictionary:
	for arg in args:
		if arg.begins_with("--query-error="):
			return {"ok": false, "error": arg.trim_prefix("--query-error=")}
	for arg in args:
		if arg.begins_with("--query-ranges="):
			var ranges := []
			var encoded := arg.trim_prefix("--query-ranges=")
			if encoded != "":
				for span in encoded.split(","):
					var bounds := span.split("-")
					if bounds.size() == 2:
						ranges.append([int(bounds[0]), int(bounds[1])])
			return {"ok": true, "ranges": ranges}
	return {}


func _value(args: PackedStringArray, prefix: String, fallback: String) -> String:
	for arg in args:
		if arg.begins_with(prefix):
			return arg.trim_prefix(prefix)
	return fallback


func _emit(record: Dictionary) -> void:
	print("INSTANCE_LOCK_JSON " + JSON.stringify(record))

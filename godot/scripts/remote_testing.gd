## Restricted server-driven tests for this fork's test builds. No remote code execution.
extends Node
const SESSION := "user://remote_testing.cfg"
var game: Node
var lobby_url := ""
var client := ""
var token := ""
var status := {}
var command := {}
var busy := false
var next_poll := 0
var last_run := ""
var started_at := 0
var stopping := false
var owns_game := false
var transport_errors := 0
var http: HTTPRequest

func _ready() -> void:
	lobby_url = game._online_lobby_url()
	var saved := ConfigFile.new()
	saved.load(SESSION)
	client = str(saved.get_value("test", "client", ""))
	token = str(saved.get_value("test", "token", ""))
	last_run = str(saved.get_value("test", "last_run", ""))
	if client.is_empty():
		client = Crypto.new().generate_random_bytes(16).hex_encode()
	if not last_run.is_empty():
		status = {"run": last_run, "state": "failed", "error": "client restarted during or after test"}
	http = HTTPRequest.new()
	http.timeout = 10.0
	http.max_redirects = 0
	http.body_size_limit = 65536
	add_child(http)
	http.request_completed.connect(_done)
	print("GAME REMOTE TEST client registered locally: ", client)

func _save() -> void:
	var saved := ConfigFile.new()
	saved.set_value("test", "client", client)
	saved.set_value("test", "token", token)
	saved.set_value("test", "last_run", last_run)
	if saved.save(SESSION) != OK:
		printerr("GAME REMOTE TEST cannot save client identity")
	if OS.get_name() in ["macOS", "Linux"]:
		FileAccess.set_unix_permissions(SESSION, 384)

func _process(_delta: float) -> void:
	if not command.is_empty() and status.get("state") not in ["passed", "desync", "failed"]:
		if Time.get_ticks_msec() - started_at > 3600000 or Time.get_unix_time_from_system() * 1000 > float(command.expiresAt):
			status.state = "failed"
			status.error = "test deadline exceeded"
		elif game._net_state == "playing":
			var net: Dictionary = game._world.net_status()
			status.state = "playing"
			status.frame = int(net.get("frame", 0))
			status.crc_checks = int(net.get("crc_checks_passed", 0))
			status.scripted_commands = int(net.get("scripted_commands", 0))
			if not net.get("desyncs", []).is_empty():
				status.state = "desync"
			elif status.frame >= int(command.frames) + int(command.get("grace", 10)) and status.crc_checks >= floori(float(command.frames) / float(command.get("crcInterval", 100))) and status.scripted_commands > 0:
				status.state = "passed"
		elif status.get("state") == "playing":
			status.state = "failed"
			status.error = "game ended before the test completed"
	if not command.is_empty() and status.get("state") in ["passed", "desync", "failed"] and not stopping:
		_stop.call_deferred()
	if busy or Time.get_ticks_msec() < next_poll:
		return
	busy = true
	var path := "/v1/testing/poll" if not token.is_empty() else "/v1/auth/guest"
	var body := {"client": client, "platform": OS.get_name(), "commit": str(Release.info.commit()) if Release.info != null else "unknown", "status": status}
	if token.is_empty():
		body = {"name": "Test " + OS.get_name() + " " + client.left(6)}
	var headers := PackedStringArray(["Content-Type: application/json"])
	if not token.is_empty():
		headers.append("Authorization: Bearer " + token)
	var error := http.request(lobby_url + path, headers, HTTPClient.METHOD_POST, JSON.stringify(body))
	if error != OK:
		_done(HTTPRequest.RESULT_CANT_CONNECT, 0, PackedStringArray(), PackedByteArray())

func _done(result: int, code: int, _headers: PackedStringArray, bytes: PackedByteArray) -> void:
	busy = false
	next_poll = Time.get_ticks_msec() + 2000
	if result != HTTPRequest.RESULT_SUCCESS or code != 200:
		transport_errors += 1
		if transport_errors == 1:
			printerr("GAME REMOTE TEST control server unavailable (HTTP %d); retrying" % code)
		return
	transport_errors = 0
	var data = JSON.parse_string(bytes.get_string_from_utf8())
	if not data is Dictionary or data.get("v") != 1:
		printerr("GAME REMOTE TEST invalid server response")
		return
	if token.is_empty():
		token = str(data.get("token", ""))
		_save()
		return
	var next = data.get("command")
	if not next is Dictionary:
		return
	if next.get("action") == "stop" and next.get("run") == last_run:
		_stop.call_deferred()
	elif next.get("action") == "run":
		if next.get("run") != last_run:
			_start(next)
		elif status.get("state") == "waiting" and command.get("role") == "join" and game._online == null and next.has("game"):
			command = next
			_begin()

func _start(next: Dictionary) -> void:
	if not str(next.get("run", "")).is_valid_hex_number(false) or str(next.get("run", "")).length() != 16 or next.get("role") not in ["host", "join"] or not str(next.get("map", "")).begins_with("maps/") or int(next.get("frames", 0)) < 100 or int(next.get("frames", 0)) > 18000:
		printerr("GAME REMOTE TEST refused invalid command")
		return
	last_run = str(next.get("run", ""))
	_save() # A repeated delivery or client restart must never execute the run again.
	command = next
	status = {"run": last_run, "state": "waiting"}
	started_at = Time.get_ticks_msec()
	if not game._net_state.is_empty() or game._state != game.State.MENU or game._online != null or not game._clean_main_menu():
		status.state = "failed"
		status.error = "client is busy; return it to the menu before testing"
		return
	if next.role == "host" or next.has("game"):
		_begin()

func _begin() -> void:
	owns_game = true
	game._sync_simulation_profile()
	game._net_script = true
	var driver = load("res://scripts/online_lobby.gd").new()
	driver.lobby_url = game._online_lobby_url()
	driver.token = token
	driver.profile = game._world.net_profile_digest()
	driver.mode = "uber" if game._world.simulation_profile() == "uber" else "normal"
	if command.role == "host":
		driver.host_setup = {"name": "Remote test " + last_run, "players": 2, "setup": {"map": command.map, "slots": [{"state": "player", "faction": -1, "color": 0, "startPos": 0, "team": 0}, {"state": "open", "faction": -1, "color": 1, "startPos": 1, "team": 1}]}}
	else:
		driver.join_id = str(command.game)
	game._online = driver
	game.add_child(driver)
	driver.started.connect(game._on_online_started)
	driver.failed.connect(func(message):
		status.state = "failed"
		status.error = str(message).left(512))
	driver.game_created.connect(func(id): status.game = id)
	status.state = "lobby"
	driver.begin()

func _stop() -> void:
	if stopping or command.is_empty():
		return
	if not owns_game:
		command = {}
		return
	stopping = true
	await get_tree().create_timer(4.0).timeout # let automatic desync diagnostics capture both halves
	if game._online != null and not game._online._game_id.is_empty():
		var leave := HTTPRequest.new()
		leave.timeout = 10.0
		leave.max_redirects = 0
		leave.body_size_limit = 65536
		add_child(leave)
		var error := leave.request(lobby_url + "/v1/games/" + game._online._game_id + "/leave", PackedStringArray(["Content-Type: application/json", "Authorization: Bearer " + token]), HTTPClient.METHOD_POST, "{}")
		if error == OK:
			await leave.request_completed
		leave.queue_free()
	if game._state == game.State.PLAYING:
		game._exit_to_score_screen()
	if game._online != null:
		game._online.queue_free()
		game._online = null
	game._net_script = false
	command = {}
	if game._state == game.State.SCORE:
		game._score_continue()
	owns_game = false
	stopping = false

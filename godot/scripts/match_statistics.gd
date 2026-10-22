## OpenBFME. GPL-3.0. Durable, opt-in, authenticated end-of-match reports.
## Statistics are read from the existing score screen; this node never changes simulation state.
extends Node

const MAX_BYTES := 512 * 1024
const MAX_PENDING := 32
var lobby_url := ""
var endpoint := "statistics"
var path := "user://online_statistics_outbox.json"
var sharing: Callable
var pending: Array = []
var problem := ""
var requests_sent := 0
var retry_seconds := 2.0
var _http: HTTPRequest
var _busy := false
var _next := 0.0
var _attempts := 0
var _blocked := {}

func _ready() -> void:
	_http = HTTPRequest.new()
	_http.timeout = 20.0
	_http.max_redirects = 0
	_http.body_size_limit = 65536
	add_child(_http)
	_http.request_completed.connect(_done)
	if FileAccess.file_exists(path):
		var saved = JSON.parse_string(FileAccess.get_file_as_string(path))
		if not saved is Dictionary or saved.get("v") != 1 or not saved.get("pending") is Array or saved.pending.size() > MAX_PENDING:
			_stop("Statistics outbox is invalid; retained for recovery.")
			return
		for entry in saved.pending:
			if not _valid_entry(entry):
				_stop("Statistics outbox contains an invalid report; retained for recovery.")
				return
		pending = saved.pending

func _stop(message: String) -> void:
	problem = message
	printerr("GAME " + endpoint.to_upper() + ": ", message)

func _valid_entry(entry) -> bool:
	return entry is Dictionary and entry.get("id") is String and entry.id.length() == 16 and entry.id.to_lower() == entry.id and entry.id.is_valid_hex_number(false) and entry.get("url") is String and load("res://scripts/online_lobby.gd").lobby_url_problem(entry.url).is_empty() and entry.get("token") is String and not entry.token.is_empty() and entry.token.length() <= 4096 and entry.get("body") is Dictionary and JSON.stringify(entry.body).to_utf8_buffer().size() <= MAX_BYTES

func _persist(next: Array) -> bool:
	var temporary := path + ".tmp"
	var file := FileAccess.open(temporary, FileAccess.WRITE)
	if file == null:
		_stop("Cannot write statistics outbox: " + error_string(FileAccess.get_open_error()))
		return false
	if OS.get_name() in ["Linux", "macOS"]:
		var permission_error := FileAccess.set_unix_permissions(temporary, 384) # 0600: queued lobby credentials stay private
		if permission_error != OK:
			file.close()
			_stop("Cannot protect statistics outbox: " + error_string(permission_error))
			return false
	file.store_string(JSON.stringify({"v": 1, "pending": next}))
	file.flush()
	var write_error := file.get_error()
	file.close()
	if write_error != OK:
		_stop("Cannot save statistics outbox: " + error_string(write_error))
		return false
	var rename_error := DirAccess.rename_absolute(ProjectSettings.globalize_path(temporary), ProjectSettings.globalize_path(path))
	if rename_error != OK:
		_stop("Cannot replace statistics outbox: " + error_string(rename_error))
		return false
	pending = next
	return true

func enqueue(context: Dictionary, body: Dictionary) -> bool:
	if not sharing.is_valid() or not sharing.call():
		return false
	var entry := {"id": context.get("id", ""), "url": context.get("url", ""), "token": context.get("token", ""), "body": body}
	if not problem.is_empty() or not _valid_entry(entry):
		printerr("GAME " + endpoint.to_upper() + " report could not be queued: invalid context, oversized body, or unavailable outbox.")
		return false
	for old in pending:
		if old.id == entry.id and old.url == entry.url:
			if JSON.stringify(old.body) != JSON.stringify(body):
				printerr("GAME " + endpoint.to_upper() + " duplicate game has a different snapshot; existing report retained.")
				return false
			return true
	if pending.size() == MAX_PENDING:
		printerr("GAME " + endpoint.to_upper() + " outbox has 32 pending games; existing reports were retained and will keep uploading.")
		return false
	var next := pending.duplicate(true)
	next.append(entry)
	return _persist(next)

func _process(_delta: float) -> void:
	if not problem.is_empty() or not sharing.is_valid():
		return
	if not sharing.call():
		if _busy:
			_http.cancel_request()
			_busy = false
		if not pending.is_empty():
			_persist([])
		return
	if _busy or pending.is_empty() or Time.get_ticks_msec() / 1000.0 < _next:
		return
	# One blocked identity must not prevent other matches/accounts from reporting.
	var entry: Dictionary = {}
	for candidate in pending:
		if not _blocked.has(candidate.id + candidate.url):
			entry = candidate
			break
	if entry.is_empty():
		return
	_active = entry
	_busy = true
	requests_sent += 1
	lobby_url = entry.url
	var error := _http.request(lobby_url.trim_suffix("/") + "/v1/games/" + entry.id + "/" + endpoint, PackedStringArray(["Content-Type: application/json", "Authorization: Bearer " + entry.token]), HTTPClient.METHOD_POST, JSON.stringify(entry.body))
	if error != OK:
		_done(HTTPRequest.RESULT_CANT_CONNECT, 0, PackedStringArray(), PackedByteArray())

var _active: Dictionary = {}

func _done(result: int, code: int, _headers: PackedStringArray, bytes: PackedByteArray) -> void:
	_busy = false
	var parser := JSON.new()
	var response = parser.data if not bytes.is_empty() and parser.parse(bytes.get_string_from_utf8()) == OK else null
	if result == HTTPRequest.RESULT_SUCCESS and code == 200 and response is Dictionary and response.get("v") == 1 and response.get("stored") == true:
		var next: Array = pending.filter(func(entry): return entry.id != _active.id or entry.url != _active.url)
		if _persist(next):
			print("GAME " + endpoint.to_upper() + " stored: ", _active.id, " (client-reported)")
		_attempts = 0
		_next = 0.0
		return
	_attempts += 1
	var permanent := code in [400, 401, 403, 404, 409, 413] or (code >= 300 and code < 400) or result == HTTPRequest.RESULT_REDIRECT_LIMIT_REACHED
	if permanent or _attempts == 12:
		_blocked[_active.id + _active.url] = true
		_attempts = 0
		printerr("GAME " + endpoint.to_upper() + " not accepted (HTTP %d); report retained privately for recovery: %s" % [code, _active.id])
	else:
		printerr("GAME " + endpoint.to_upper() + " upload interrupted (HTTP %d); retained for retry: %s" % [code, _active.id])
	_next = Time.get_ticks_msec() / 1000.0 + minf(60.0, retry_seconds * pow(2.0, _attempts - 1))

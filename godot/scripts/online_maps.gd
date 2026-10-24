## OpenBFME. GPL-3.0.
## Map transfer in online games (engine GameNetwork/MapPackage.h; the LAN lobby sends maps itself, LANAPI's MAP_REQUEST / MAP_DATA). The players of an
## online game are not connected to each other before it starts, so the lobby service keeps the map: the host stores its custom map's package
## (POST /v1/maps/<digest>, the digest its setup key names), and a player who does not have it fetches it (GET /v1/maps/<digest>, again every few
## seconds until the host's upload is there), installs it (RetailFileSystem.install_map_package checks the bytes against the key) and can then be Ready
## (the client refuses Ready until then: OnlineLobbyClient::setReady). A map of the game itself or of a mod is never sent.
## A child of scripts/online_http.gd (it goes with the online screens' transport).
extends Node

var shell: Object = null   ## the AptMenuPlayer (online_status, online_map_note)
var fs: RefCounted = null  ## the RetailFileSystem
var lobby_url := ""

const RETRY_S := 3.0       ## a map the host has not uploaded yet is asked for again after this
const MAX_BYTES := 64 * 1024 * 1024 ## MapPackage::kMaxBytes (the lobby's store may keep less)

var _http: HTTPRequest
var _busy := false
var _uploaded := ""        ## the map key stored for this game already
var _failed := ""          ## a map key that cannot be sent or received (said once)
var _waiting_said := ""
var _next_try := 0.0
var _game := ""


static func key_digest(key: String) -> String:
	var m := RegEx.create_from_string("^maps/openbfme_map_([0-9a-f]{32})/openbfme_map_([0-9a-f]{32})\\.map$").search(key)
	return m.get_string(1) if m != null and m.get_string(1) == m.get_string(2) else ""


func _ready() -> void:
	_http = HTTPRequest.new()
	_http.timeout = 120.0
	_http.max_redirects = 0
	_http.body_size_limit = MAX_BYTES + 65536
	add_child(_http)


func _note(text: String) -> void:
	print("GAME ONLINE map: ", text)
	if shell != null:
		shell.online_map_note(text)


func _process(_delta: float) -> void:
	if shell == null or fs == null or _busy or lobby_url.is_empty():
		return
	var st: Dictionary = shell.online_status()
	if not st.get("open", false) or not st.get("in_game", false):
		_game = ""
		return
	if str(st.game_id) != _game:
		_game = str(st.game_id)
		_uploaded = ""
		_failed = ""
		_waiting_said = ""
		_next_try = 0.0
	var key := str(st.get("map", ""))
	var digest := key_digest(key)
	if digest.is_empty() or key == _failed:
		return
	var token := str(st.get("session_token", ""))
	var now := Time.get_ticks_msec() / 1000.0
	if st.get("host", false):
		if key != _uploaded and now >= _next_try:
			_upload(key, digest, token)
	elif not st.get("has_map", true) and now >= _next_try:
		_download(key, digest, token)


func _headers(token: String, content: String) -> PackedStringArray:
	var h := PackedStringArray(["content-type: " + content])
	if not token.is_empty():
		h.append("authorization: Bearer " + token)
	return h


func _url(digest: String) -> String:
	return lobby_url.trim_suffix("/") + "/v1/maps/" + digest


func _upload(key: String, digest: String, token: String) -> void:
	var bytes: PackedByteArray = fs.map_package(key)
	if bytes.is_empty():
		_failed = key
		_note("This map cannot be sent to the other players (it could not be read).")
		return
	_busy = true
	if _http.request_raw(_url(digest), _headers(token, "application/octet-stream"), HTTPClient.METHOD_POST, bytes) != OK:
		_busy = false
		_failed = key
		_note("The map could not be uploaded: the lobby cannot be reached.")
		return
	var r: Array = await _http.request_completed
	_busy = false
	var code: int = r[1]
	if r[0] == HTTPRequest.RESULT_SUCCESS and code == 200:
		_uploaded = key
		_note("The map was uploaded for the other players (%d KB)." % (bytes.size() / 1024))
	elif code == 413:
		_failed = key
		_note("The map is too big for this lobby server (%d KB): the other players need to install it themselves." % (bytes.size() / 1024))
	else:
		_next_try = Time.get_ticks_msec() / 1000.0 + RETRY_S # a lost connection or a busy lobby: again after the pause
		if code >= 400 and code < 500 and code != 429:
			_failed = key # refused: trying again would be refused too
		_note("The map could not be uploaded (%s)." % _why(r))


func _download(key: String, digest: String, token: String) -> void:
	_busy = true
	if _http.request(_url(digest), _headers(token, "application/json"), HTTPClient.METHOD_GET) != OK:
		_busy = false
		_next_try = Time.get_ticks_msec() / 1000.0 + RETRY_S
		return
	var r: Array = await _http.request_completed
	_busy = false
	var code: int = r[1]
	if r[0] == HTTPRequest.RESULT_SUCCESS and code == 200:
		var body: PackedByteArray = r[3]
		var res: Dictionary = fs.install_map_package(body, key)
		if res.get("ok", false):
			_note("The map was received and installed: you can be Ready now.")
		else:
			_failed = key
			_note("The received map could not be installed: " + str(res.get("error", "")))
		return
	_next_try = Time.get_ticks_msec() / 1000.0 + RETRY_S
	if code == 404:
		if _waiting_said != key:
			_waiting_said = key
			_note("You do not have this map: waiting for the host to upload it...")
	elif code >= 400 and code < 500 and code != 429:
		_failed = key
		_note("The map could not be downloaded (%s)." % _why(r))


func _why(r: Array) -> String:
	if r[0] != HTTPRequest.RESULT_SUCCESS:
		return "no answer from the lobby (%d)" % r[0]
	var body: PackedByteArray = r[3]
	var j = JSON.parse_string(body.slice(0, 2048).get_string_from_utf8())
	return str(j.get("message", "HTTP %d" % r[1])) if j is Dictionary else "HTTP %d" % r[1]

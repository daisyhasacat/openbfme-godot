## Background per-file diagnostics; isolated from join acceptance and simulation.
extends Node
signal report_ready(text: String)
signal finished()
var fs: RefCounted
var lobby_url := ""
var report_directory := "user://"
var _thread: Thread
var _files: Array = []
var _error := ""
var _jobs: Array = []
var _busy := false

func begin(game: String, token: String, hosting: bool, reason: String = "") -> void:
	if not reason.is_empty():
		if not token.is_empty(): reason = reason.replace(token, "<redacted>")
		report_ready.emit(AptMenuPlayer.online_redact(reason.left(8192)))
	if not game.is_valid_hex_number() or game.length() != 16:
		report_ready.emit("Per-file comparison unavailable: this lobby does not provide file diagnostics. Update the lobby and both clients, then recreate the room.")
		return
	_jobs.append({"game": game, "token": token, "hosting": hosting})
	report_ready.emit("Computing individual archive, archive-entry and engine file checksums in the background…")
	if _thread == null and _files.is_empty() and _error.is_empty():
		_thread = Thread.new()
		var err := _thread.start(Callable(fs, "compatibility_checksums"))
		if err != OK:
			_error = "Could not start file checksum worker: " + error_string(err)
			_thread = null

func _process(_delta: float) -> void:
	if _thread != null and not _thread.is_alive():
		var result: Dictionary = _thread.wait_to_finish()
		_thread = null
		_error = str(result.get("error", ""))
		_files = result.get("files", [])
	if not _busy and _thread == null and not _jobs.is_empty():
		_busy = true
		_run(_jobs.pop_front())

func _exit_tree() -> void:
	if _thread != null and _thread.is_started():
		_thread.wait_to_finish()

func _request(job: Dictionary, method: HTTPClient.Method, page: int, body: Dictionary = {}) -> Dictionary:
	var http := HTTPRequest.new()
	http.timeout = 20.0
	http.max_redirects = 0
	http.body_size_limit = 262144
	add_child(http)
	var headers := PackedStringArray(["content-type: application/json", "authorization: Bearer " + str(job.token)])
	var err := http.request(lobby_url.trim_suffix("/") + "/v1/games/%s/compatibility?page=%d" % [job.game, page], headers, method, JSON.stringify(body) if method == HTTPClient.METHOD_POST else "")
	if err != OK:
		http.queue_free()
		return {"error": error_string(err)}
	var answer: Array = await http.request_completed
	http.queue_free()
	if answer[0] != HTTPRequest.RESULT_SUCCESS:
		return {"error": "Diagnostic transport failed (%s)" % answer[0]}
	var decoded = JSON.parse_string(answer[3].get_string_from_utf8())
	if not decoded is Dictionary:
		return {"error": "Invalid diagnostic response"}
	if answer[1] != 200:
		var message := str(decoded.get("message", "Diagnostic HTTP %s" % answer[1])).replace(str(job.token), "<redacted>")
		return {"error": AptMenuPlayer.online_redact(message)}
	return decoded

func _run(job: Dictionary) -> void:
	if not _error.is_empty():
		report_ready.emit("File compatibility diagnostics failed: " + _error)
		_complete()
		return
	var local_path := report_directory.path_join("compatibility-%s-local.json" % job.game)
	var saved := _save(local_path, _files)
	if not saved.is_empty():
		report_ready.emit(saved)
	if job.hosting:
		var pages := _pages(_files)
		for i in pages.size():
			var result := await _request(job, HTTPClient.METHOD_POST, i, {"page": i, "total": pages.size(), "rows": pages[i]})
			if result.has("error"):
				report_ready.emit("Could not share host per-file hashes: %s\nLocal inventory: %s" % [result.error, local_path])
				_complete()
				return
		report_ready.emit("Host per-file checksums ready: %d files. Local inventory: %s" % [_files.size(), local_path])
	else:
		report_ready.emit("Comparing individual files with the host; waiting for its background checksum scan…")
		var remote: Array = []
		var remote_bytes := 0
		var ready := false
		for _attempt in 90:
			var first := await _request(job, HTTPClient.METHOD_GET, 0)
			if first.has("error"):
				report_ready.emit("Host file comparison unavailable: %s\nLocal inventory: %s" % [first.error, local_path])
				_complete()
				return
			if first.get("ready", false):
				remote = first.get("rows", [])
				remote_bytes = JSON.stringify(remote).to_utf8_buffer().size()
				var total := int(first.get("total", 0))
				if total < 1 or total > 2048 or remote.size() > 256:
					report_ready.emit("Invalid host diagnostic page count")
					_complete()
					return
				for page in range(1, total):
					var response := await _request(job, HTTPClient.METHOD_GET, page)
					if response.has("error") or not response.get("ready", false):
						report_ready.emit("Host diagnostic snapshot incomplete: " + str(response.get("error", "expired")))
						_complete()
						return
					var chunk = response.get("rows", [])
					remote_bytes += JSON.stringify(chunk).to_utf8_buffer().size()
					if not chunk is Array or chunk.size() > 256 or remote_bytes > 32 * 1024 * 1024:
						report_ready.emit("Host diagnostic snapshot exceeds the file/page limits")
						_complete()
						return
					remote.append_array(chunk)
				ready = true
				break
			await get_tree().create_timer(2.0).timeout
		if ready:
			var report := compare(_files, remote)
			var remote_path := report_directory.path_join("compatibility-%s-host.json" % job.game)
			var report_path := report_directory.path_join("compatibility-%s.txt" % job.game)
			var save_error := _save(remote_path, remote)
			var file := FileAccess.open(report_path, FileAccess.WRITE)
			if file == null:
				save_error += " Could not save full comparison: " + error_string(FileAccess.get_open_error())
			else:
				file.store_string(report)
			# The Lua editor retains 200K characters; keep the full output on disk.
			report_ready.emit(report.left(140000) + ("\nConsole display limited; full report saved below." if report.length() > 140000 else "") + "\nLocal checksums: " + local_path + "\nHost checksums: " + remote_path + "\nFull comparison: " + report_path + "\n" + save_error)
		else:
			report_ready.emit("Host checksums were not ready after three minutes. The host snapshot may be unavailable or expired; ask the host to recreate the room. Local inventory: " + local_path)
	_complete()

static func _pages(rows: Array) -> Array:
	var pages: Array = []
	var page: Array = []
	var bytes := 0
	for row in rows:
		var size := JSON.stringify(row).to_utf8_buffer().size() + 1
		if not page.is_empty() and (bytes + size > 48000 or page.size() == 256):
			pages.append(page)
			page = []
			bytes = 0
		page.append(row)
		bytes += size
	if not page.is_empty(): pages.append(page)
	return pages

static func _save(path: String, rows: Array) -> String:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null: return "Could not save checksum inventory: " + error_string(FileAccess.get_open_error())
	file.store_string(JSON.stringify(rows, "\t"))
	return ""

static func compare(local: Array, remote: Array) -> String:
	var a := _index(local)
	var b := _index(remote)
	if a.has("error") or b.has("error"):
		return "File comparison failed: " + str(a.get("error", b.get("error", "")))
	var keys: Dictionary = a.duplicate()
	keys.merge(b)
	var names: Array = keys.keys()
	names.sort()
	var lines := PackedStringArray(["PER-FILE COMPATIBILITY COMPARISON", "Archive/entry hashes: MD5. Engine source hashes: SHA-256. Source sizes are not recorded."])
	var changed := 0
	for name in names:
		if a.get(name) == b.get(name): continue
		changed += 1
		lines.append(str(name))
		lines.append("  You:  " + str(a.get(name, "MISSING")))
		lines.append("  Host: " + str(b.get(name, "MISSING")))
	lines.append("%d differing files/settings; %d local records; %d host records." % [changed, local.size(), remote.size()])
	if changed == 0: lines.append("All supplied file checksums match. The join refusal may concern build-ID overrides or compatibility metadata rather than file contents.")
	return "\n".join(lines)

static func _index(rows: Array) -> Dictionary:
	var result := {}
	for row in rows:
		if not row is Array or row.size() != 3 or not row[0] is String or not row[1] is String or result.has(row[0]):
			return {"error": "Invalid or duplicate file checksum record"}
		if row[0].length() > 1024 or not (row[1].length() == 32 or row[1].length() == 64) or not row[1].is_valid_hex_number():
			return {"error": "Invalid file checksum or path"}
		if not (row[2] is int or row[2] is float) or row[2] < 0 or row[2] > 9007199254740991 or floor(row[2]) != row[2]:
			return {"error": "Invalid file byte size"}
		result[row[0]] = "%s (%s bytes)" % [row[1], int(row[2])]
	return result

func is_pending() -> bool:
	return _thread != null or _busy or not _jobs.is_empty()

func _complete() -> void:
	_busy = false
	finished.emit()

extends SceneTree
const Diagnostics = preload("res://scripts/online_compatibility.gd")

class FixtureFiles extends RefCounted:
	var changed := false
	func compatibility_checksums() -> Dictionary:
		var rows: Array = []
		for i in 600:
			rows.append(["entry/test:ini.big/data/ini/file-%d.ini" % i, ("b" if changed and i == 599 else "a").repeat(32), i])
		return {"error": "", "files": rows}

var url := ""
var reports: Array[String] = []
var host: Node
var joiner: Node

func _initialize() -> void:
	url = OS.get_environment("OPENBFME_TEST_LOBBY")
	if url.is_empty():
		printerr("OPENBFME_TEST_LOBBY is required")
		quit(1)
		return
	_run.call_deferred()

func _call(path: String, body: Dictionary, token: String = "") -> Dictionary:
	var http := HTTPRequest.new()
	root.add_child(http)
	var headers := PackedStringArray(["content-type: application/json"])
	if not token.is_empty(): headers.append("authorization: Bearer " + token)
	assert(http.request(url + path, headers, HTTPClient.METHOD_POST, JSON.stringify(body)) == OK)
	var answer: Array = await http.request_completed
	http.queue_free()
	assert(answer[0] == HTTPRequest.RESULT_SUCCESS)
	return JSON.parse_string(answer[3].get_string_from_utf8())

func _run() -> void:
	var a := await _call("/v1/auth/guest", {"name": "FileHost"})
	var b := await _call("/v1/auth/guest", {"name": "FileJoin"})
	var made := await _call("/v1/games", {"name": "File exchange", "profile": "a".repeat(32), "setup": {"map": "maps/test/test.map", "slots": [{"state": "player"}, {"state": "open"}]}}, a.token)
	assert(made.has("id"))
	var refused := await _call("/v1/games/%s/join" % made.id, {"profile": "b".repeat(32)}, b.token)
	assert(refused.get("compatibility_game", "") == made.id)
	var directory := OS.get_environment("OPENBFME_TEST_REPORT_DIR")
	assert(not directory.is_empty())
	assert(DirAccess.make_dir_recursive_absolute(directory) == OK)
	for hosting in [true, false]:
		var node := Diagnostics.new()
		var fixture := FixtureFiles.new()
		fixture.changed = not hosting
		node.fs = fixture
		node.lobby_url = url
		node.report_directory = directory
		node.report_ready.connect(func(text: String) -> void: reports.append(text))
		root.add_child(node)
		node.begin(made.id, a.token if hosting else b.token, hosting)
		if hosting: host = node
		else: joiner = node
	for _frame in 1000:
		if reports.any(func(text: String) -> bool: return text.contains("1 differing files/settings")):
			var report: String = reports.filter(func(text: String) -> bool: return text.contains("1 differing files/settings"))[0]
			assert(report.contains("file-599.ini"))
			assert(report.contains("You:  " + "b".repeat(32)))
			assert(report.contains("Host: " + "a".repeat(32)))
			assert(FileAccess.file_exists(directory.path_join("compatibility-%s.txt" % made.id)))
			print("Per-file exchange: 600 records across three pages; changed file and both hashes verified")
			quit()
			return
		await create_timer(0.02).timeout
	printerr("File exchange timed out: ", reports)
	quit(1)

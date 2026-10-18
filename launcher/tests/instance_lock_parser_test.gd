extends SceneTree

const InstanceLock := preload("res://scripts/core/instance_lock.gd")


func _init() -> void:
	var failures := []
	var checks := 0
	var cases := [
		["English headers, CRLF, and administered footer",
			"TCP Dynamic Port Exclusions\r\nStart Port    End Port\r\n----------    --------\r\n64138 64237\r\n* - Administered port exclusions\r\n",
			[[64138, 64237]]],
		["localized headers and an administered range row",
			"Rangos de exclusion TCP\r\nPuerto inicial    Puerto final\r\n----------    --------\r\n50000 50010 *\r\n",
			[[50000, 50010]]],
		["valid empty table", "Excluded ports\r\nStart End\r\n----- -----\r\n", []],
	]
	for item in cases:
		checks += 1
		var parsed: Dictionary = InstanceLock.parse_exclusions(item[1])
		if not parsed.get("ok", false) or parsed.get("ranges", []) != item[2]:
			failures.append(item[0])
	for item in [
		["malformed row", "Header\r\nStart End\r\n----- -----\r\nnot-a-range\r\n"],
		["reversed range", "Header\r\nStart End\r\n----- -----\r\n8 7\r\n"],
		["range below valid bounds", "Header\r\nStart End\r\n----- -----\r\n0 5\r\n"],
		["range above valid bounds", "Header\r\nStart End\r\n----- -----\r\n1 65536\r\n"],
		["missing separator", "Header\r\nStart End\r\n64138 64237\r\n"],
	]:
		checks += 1
		var parsed: Dictionary = InstanceLock.parse_exclusions(item[1])
		if parsed.get("ok", false):
			failures.append(item[0])
	print("INSTANCE_LOCK_JSON " + JSON.stringify({"event": "parser", "checks": checks, "failures": failures}))
	quit(0 if failures.is_empty() else 1)
